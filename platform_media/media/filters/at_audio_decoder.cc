// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2015 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA.

#include "media/filters/at_audio_decoder.h"

#include <algorithm>
#include <AudioToolbox/AudioToolbox.h>

#include "base/bind.h"
#include "base/features/features.h"
#include "base/location.h"
#include "base/mac/mac_logging.h"
#include "base/mac/mac_util.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "media/base/audio_buffer.h"
#include "media/base/audio_discard_helper.h"
#include "media/base/demuxer_stream.h"
#include "media/base/mac/framework_type_conversions.h"
#include "media/base/pipeline_stats.h"
#include "media/base/platform_mime_util.h"
#include "media/filters/at_aac_helper.h"
#include "media/filters/at_mp3_helper.h"

namespace media {

namespace {

const SampleFormat kOutputSampleFormat = kSampleFormatF32;

// Custom error codes returned from ProvideData() and passed on to the caller
// of AudioConverterFillComplexBuffer().
const OSStatus kDataConsumed = 'CNSM';   // No more input data currently.
const OSStatus kInvalidArgs = 'IVLD';    // Unexpected callback arguments.

struct ScopedAudioFileStreamIDTraits {
  static AudioFileStreamID Retain(AudioFileStreamID stream_id) {
    NOTREACHED() << "Only compatible with ASSUME policy";
    return stream_id;
  }
  static void Release(AudioFileStreamID stream_id) {
    AudioFileStreamClose(stream_id);
  }
  static AudioFileStreamID InvalidValue() { return nullptr; }
};

using ScopedAudioFileStreamID =
    base::ScopedTypeRef<AudioFileStreamID, ScopedAudioFileStreamIDTraits>;

// Wraps an input buffer and some metadata.  Used as the type of the user data
// passed between the caller of AudioConverterFillComplexBuffer() and the
// ProvideData() callback.
struct InputData {
  // Strip the ADTS header from the buffer.  Required for AudioConverter to
  // accept the input data.
  InputData(const DecoderBuffer& buffer, int channel_count, size_t header_size)
      : data(buffer.data() + header_size),
        data_size(buffer.data_size() - header_size),
        channel_count(channel_count),
        packet_description({0}),
        consumed(false) {
    DCHECK_GE((int) buffer.data_size(), base::checked_cast<int>(header_size));
    packet_description.mDataByteSize = data_size;
  }

  // Constructs an InputData object representing "no data".
  InputData()
      : data(nullptr),
        data_size(0),
        channel_count(0),
        packet_description({0}),
        consumed(false) {}

  const void* data;
  size_t data_size;
  int channel_count;
  AudioStreamPacketDescription packet_description;
  bool consumed;
};

// Used as the data-supply callback for AudioConverterFillComplexBuffer().
OSStatus ProvideData(AudioConverterRef inAudioConverter,
                     UInt32* ioNumberDataPackets,
                     AudioBufferList* ioData,
                     AudioStreamPacketDescription** outDataPacketDescription,
                     void* inUserData) {
  DVLOG(5) << "AudioConverter wants " << *ioNumberDataPackets
           << " input frames";

  InputData* const input_data = reinterpret_cast<InputData*>(inUserData);
  DCHECK(input_data);
  if (input_data->consumed) {
    DVLOG(5) << "But there is no more input data";
    *ioNumberDataPackets = 0;
    return kDataConsumed;
  }

  if (ioData->mNumberBuffers != 1u) {
    DVLOG(1) << "Expected 1 output buffer, got " << ioData->mNumberBuffers;
    return kInvalidArgs;
  }

  DVLOG(5) << "Providing " << input_data->data_size << " bytes";

  ioData->mBuffers[0].mNumberChannels = input_data->channel_count;
  ioData->mBuffers[0].mDataByteSize = input_data->data_size;
  ioData->mBuffers[0].mData = const_cast<void*>(input_data->data);

  if (outDataPacketDescription)
    *outDataPacketDescription = &input_data->packet_description;

  input_data->consumed = true;
  return noErr;
}

std::unique_ptr<ATCodecHelper> CreateCodecHelper(AudioCodec codec) {
  switch (codec) {
    case kCodecAAC:
      return base::WrapUnique(new ATAACHelper);
    case kCodecMP3:
      return base::IsFeatureEnabled(base::kFeatureMseAudioMpegAac)
                 ? base::WrapUnique(new ATMP3Helper)
                 : nullptr;
    default:
      return nullptr;
  }
}

// Fills out the output format to meet Chrome pipeline requirements.
void GetOutputFormat(const AudioStreamBasicDescription& input_format,
                     AudioStreamBasicDescription* output_format) {
  DVLOG(1) << __func__;

  memset(output_format, 0, sizeof(*output_format));
  output_format->mFormatID = kAudioFormatLinearPCM;
  output_format->mFormatFlags = kLinearPCMFormatFlagIsFloat;
  output_format->mSampleRate = input_format.mSampleRate;
  output_format->mChannelsPerFrame = input_format.mChannelsPerFrame;
  output_format->mBitsPerChannel = 32;
  output_format->mBytesPerFrame =
      output_format->mChannelsPerFrame * output_format->mBitsPerChannel / 8;
  output_format->mFramesPerPacket = 1;
  output_format->mBytesPerPacket = output_format->mBytesPerFrame;
}

// Adds |padding_frame_count| frames of silence to the front of |buffer| and
// returns the resulting buffer.  This is used when we need to "fix" the
// behavior of AudioConverter wrt codec delay handling.  If AudioConverter
// strips the codec delay internally, it's all fine unless we are decoding
// audio appended via MSE.  In this case, only the initial delay gets stripped,
// and the one after the append is not.  AudioDiscardHelper can do the
// stripping for us, using discard information from FrameProcessor, but then
// the codec delay must be present in the initial output buffer too, hence the
// padding that we're adding.
scoped_refptr<AudioBuffer> AddFrontPadding(
    const scoped_refptr<AudioBuffer>& buffer,
    size_t padding_frame_count) {
  DVLOG(1) << __func__;

  const scoped_refptr<AudioBuffer> result = AudioBuffer::CreateBuffer(
      kOutputSampleFormat, buffer->channel_layout(), buffer->channel_count(),
      buffer->sample_rate(), padding_frame_count + buffer->frame_count());

  const size_t padding_size =
      padding_frame_count * buffer->channel_count() *
      SampleFormatToBytesPerChannel(kOutputSampleFormat);
  uint8_t* const result_data = result->channel_data()[0];
  std::fill(result_data, result_data + padding_size, 0);

  uint8_t* const buffer_data = buffer->channel_data()[0];
  const size_t buffer_size = buffer->frame_count() * buffer->channel_count() *
                             SampleFormatToBytesPerChannel(kOutputSampleFormat);
  std::copy(buffer_data, buffer_data + buffer_size, result_data + padding_size);

  return result;
}

}  // namespace


// static
AudioConverterRef ATAudioDecoder::ScopedAudioConverterRefTraits::Retain(
    AudioConverterRef converter) {
  NOTREACHED() << "Only compatible with ASSUME policy";
  return converter;
}

// static
void ATAudioDecoder::ScopedAudioConverterRefTraits::Release(
    AudioConverterRef converter) {
  const OSStatus status = AudioConverterDispose(converter);
  if (status != noErr)
    OSSTATUS_DVLOG(1, status) << FourCCToString(status)
                              << ": Failed to dispose of AudioConverter";
}

AudioConverterRef ATAudioDecoder::ScopedAudioConverterRefTraits::InvalidValue() {
  return nullptr;
}

ATAudioDecoder::ATAudioDecoder(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : task_runner_(task_runner),
      needs_eos_workaround_(base::mac::IsOS10_9()) {}

ATAudioDecoder::~ATAudioDecoder() = default;

std::string ATAudioDecoder::GetDisplayName() const {
  return "ATAudioDecoder";
}

void ATAudioDecoder::Initialize(const AudioDecoderConfig& config,
                                CdmContext* cdm_context,
                                const InitCB& init_cb,
                                const OutputCB& output_cb) {
  DVLOG(1) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(config.IsValidConfig());

  pipeline_stats::AddDecoderClass(GetDisplayName());

  codec_helper_ = CreateCodecHelper(config.codec());
  if (!codec_helper_) {
    DVLOG(1) << "Unsupported codec: " << config.codec();
    task_runner_->PostTask(FROM_HERE, base::Bind(init_cb, false));
    return;
  }

  if (!IsPlatformAudioDecoderAvailable(config.codec())) {
    task_runner_->PostTask(FROM_HERE, base::Bind(init_cb, false));
    return;
  }

  // This decoder supports re-initialization.
  converter_.reset();

  config_ = config;
  output_cb_ = output_cb;

  ResetTimestampState();

  // Unretained() is safe, because ATCodecHelper is required to invoke the
  // callbacks synchronously.
  if (!codec_helper_->Initialize(
          config, base::Bind(&ATAudioDecoder::InitializeConverter,
                             base::Unretained(this)),
          base::Bind(&ATAudioDecoder::ConvertAudio, base::Unretained(this)))) {
    pipeline_stats::ReportAudioDecoderInitResult(false);
    task_runner_->PostTask(FROM_HERE, base::Bind(init_cb, false));
    return;
  }

  pipeline_stats::ReportAudioDecoderInitResult(true);
  task_runner_->PostTask(FROM_HERE, base::Bind(init_cb, true));
}

void ATAudioDecoder::Decode(const scoped_refptr<DecoderBuffer>& buffer,
                            const DecodeCB& decode_cb) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  const media::DecodeStatus status =
      codec_helper_->ProcessBuffer(buffer) ? media::DecodeStatus::OK : media::DecodeStatus::DECODE_ERROR;

  task_runner_->PostTask(FROM_HERE, base::Bind(decode_cb, status));
}

void ATAudioDecoder::Reset(const base::Closure& closure) {
  DVLOG(1) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());

  // There is no |converter_| if Reset() is called before Decode(), which is
  // legal.
  if (converter_) {
    const OSStatus status = AudioConverterReset(converter_);
    if (status != noErr)
      OSSTATUS_DVLOG(1, status) << FourCCToString(status)
                                << ": Failed to reset AudioConverter";
  }

  ResetTimestampState();

  task_runner_->PostTask(FROM_HERE, closure);
}

bool ATAudioDecoder::InitializeConverter(
    const AudioStreamBasicDescription& input_format,
    ScopedAudioChannelLayoutPtr input_channel_layout) {
  DVLOG(1) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());

  AudioStreamBasicDescription output_format;
  GetOutputFormat(input_format, &output_format);

  OSStatus status = AudioConverterNew(&input_format, &output_format,
                                      converter_.InitializeInto());
  if (status != noErr) {
    OSSTATUS_DVLOG(1, status) << FourCCToString(status)
                              << ": Failed to create AudioConverter";
    return false;
  }

  status = AudioConverterSetProperty(
      converter_, kAudioConverterInputChannelLayout,
      sizeof(*input_channel_layout), input_channel_layout.get());
  if (status != noErr) {
    OSSTATUS_DVLOG(1, status) << FourCCToString(status)
                              << ": Failed to set input channel layout";
    return false;
  }

  AudioChannelLayout output_channel_layout = {0};
  output_channel_layout.mChannelLayoutTag =
      ChromeChannelLayoutToCoreAudioTag(config_.channel_layout());
  status = AudioConverterSetProperty(
      converter_, kAudioConverterOutputChannelLayout,
      sizeof(output_channel_layout), &output_channel_layout);
  if (status != noErr) {
    OSSTATUS_DVLOG(1, status) << FourCCToString(status)
                              << ": Failed to set output channel layout";
    return false;
  }

  return true;
}

bool ATAudioDecoder::ConvertAudio(const scoped_refptr<DecoderBuffer>& input,
                                  size_t header_size,
                                  size_t max_output_frame_count) {

  // NOTE(espen@vivaldi.com): Added as crash fix for OSX 10.12
  if (input.get() == nullptr || input->end_of_stream() || input->data_size() == 0) {
    return true;
  }

  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(converter_);

  UInt32 output_frame_count = max_output_frame_count;

  // Pre-allocate a buffer for the maximum expected frame count.  We will let
  // the AudioConverter fill it with decoded audio, through |output_buffers|
  // defined below.
  scoped_refptr<AudioBuffer> output = AudioBuffer::CreateBuffer(
      kOutputSampleFormat, config_.channel_layout(),
      ChannelLayoutToChannelCount(config_.channel_layout()),
      config_.samples_per_second(), output_frame_count);

  InputData input_data =
      input->end_of_stream()
          // No more input data, but we must flush AudioConverter.
          ? InputData()
          // Will provide data from |input| to AudioConverter in ProvideData().
          : InputData(*input, output->channel_count(), header_size);

  AudioBufferList output_buffers;
  output_buffers.mNumberBuffers = 1;
  output_buffers.mBuffers[0].mNumberChannels = output->channel_count();
  output_buffers.mBuffers[0].mDataByteSize =
      output->frame_count() * output->channel_count() *
      SampleFormatToBytesPerChannel(kOutputSampleFormat);
  // Will put decoded data in the |output| media::AudioBuffer directly.
  output_buffers.mBuffers[0].mData = output->channel_data()[0];

  AudioStreamPacketDescription
      output_packet_descriptions[max_output_frame_count];

  OSStatus status = noErr;
  if (ApplyEOSWorkaround(input, &output_buffers)) {
    DVLOG(1)
        << "Couldn't flush AudioConverter properly on this system. Faking it";
  } else {
    status = AudioConverterFillComplexBuffer(
        converter_, &ProvideData, &input_data, &output_frame_count,
        &output_buffers, output_packet_descriptions);
  }

  if (status != noErr && status != kDataConsumed) {
    OSSTATUS_DVLOG(1, status) << FourCCToString(status)
                              << ": Failed to convert audio";
    return false;
  }

  if (output_frame_count > max_output_frame_count) {
    DVLOG(1) << "Unexpected output sample count: " << output_frame_count;
    return false;
  }

  if (!input->end_of_stream())
    queued_input_.push_back(input);

  if (output_frame_count > 0 && !queued_input_.empty()) {
    output->TrimEnd(max_output_frame_count - output_frame_count);

    const scoped_refptr<DecoderBuffer> dequeued_input = queued_input_.front();
    queued_input_.pop_front();

    const bool first_output_buffer = !discard_helper_->initialized();
    if (first_output_buffer)
      output = AddFrontPadding(output, config_.codec_delay());

    DVLOG(5) << "Decoded " << output_frame_count << " frames @"
             << dequeued_input->timestamp();

    // ProcessBuffers() computes and sets the timestamp on |output|.
    if (discard_helper_->ProcessBuffers(dequeued_input, output))
      task_runner_->PostTask(FROM_HERE, base::Bind(output_cb_, output));
  }

  return true;
}

bool ATAudioDecoder::ApplyEOSWorkaround(
    const scoped_refptr<DecoderBuffer>& input,
    AudioBufferList* output_buffers) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!needs_eos_workaround_ || !input->end_of_stream())
    return false;

  uint8_t* const data =
      reinterpret_cast<uint8_t*>(output_buffers->mBuffers[0].mData);
  const size_t data_size = output_buffers->mBuffers[0].mDataByteSize;
  std::fill(data, data + data_size, 0);

  return true;
}

void ATAudioDecoder::ResetTimestampState() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  discard_helper_.reset(new AudioDiscardHelper(config_.samples_per_second(),
                                               config_.codec_delay(), false));
  discard_helper_->Reset(config_.codec_delay());

  queued_input_.clear();
}

}  // namespace media
