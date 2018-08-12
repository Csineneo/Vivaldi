// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included message file, hence no include guard here, but see below
// for a much smaller-than-usual include guard section.

#include "gpu/config/gpu_info.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/param_traits_macros.h"
#include "media/base/decrypt_config.h"
#include "media/base/video_types.h"
#include "media/video/jpeg_decode_accelerator.h"
#include "media/video/video_decode_accelerator.h"
#include "media/video/video_encode_accelerator.h"

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

#define IPC_MESSAGE_START MediaMsgStart

IPC_ENUM_TRAITS_MAX_VALUE(media::JpegDecodeAccelerator::Error,
                          media::JpegDecodeAccelerator::LARGEST_ERROR_ENUM)
IPC_ENUM_TRAITS_MAX_VALUE(media::VideoEncodeAccelerator::Error,
                          media::VideoEncodeAccelerator::kErrorMax)
IPC_ENUM_TRAITS_MIN_MAX_VALUE(media::VideoCodecProfile,
                              media::VIDEO_CODEC_PROFILE_MIN,
                              media::VIDEO_CODEC_PROFILE_MAX)

IPC_STRUCT_BEGIN(AcceleratedJpegDecoderMsg_Decode_Params)
  IPC_STRUCT_MEMBER(int32_t, input_buffer_id)
  IPC_STRUCT_MEMBER(gfx::Size, coded_size)
  IPC_STRUCT_MEMBER(base::SharedMemoryHandle, input_buffer_handle)
  IPC_STRUCT_MEMBER(uint32_t, input_buffer_size)
  IPC_STRUCT_MEMBER(base::SharedMemoryHandle, output_video_frame_handle)
  IPC_STRUCT_MEMBER(uint32_t, output_buffer_size)
IPC_STRUCT_END()

IPC_STRUCT_BEGIN(AcceleratedVideoDecoderMsg_Decode_Params)
  IPC_STRUCT_MEMBER(int32_t, bitstream_buffer_id)
  IPC_STRUCT_MEMBER(base::SharedMemoryHandle, buffer_handle)
  IPC_STRUCT_MEMBER(uint32_t, size)
  IPC_STRUCT_MEMBER(base::TimeDelta, presentation_timestamp)
  IPC_STRUCT_MEMBER(std::string, key_id)
  IPC_STRUCT_MEMBER(std::string, iv)
  IPC_STRUCT_MEMBER(std::vector<media::SubsampleEntry>, subsamples)
IPC_STRUCT_END()

IPC_STRUCT_BEGIN(AcceleratedVideoEncoderMsg_Encode_Params)
  IPC_STRUCT_MEMBER(int32_t, frame_id)
  IPC_STRUCT_MEMBER(base::TimeDelta, timestamp)
  IPC_STRUCT_MEMBER(base::SharedMemoryHandle, buffer_handle)
  IPC_STRUCT_MEMBER(uint32_t, buffer_offset)
  IPC_STRUCT_MEMBER(uint32_t, buffer_size)
  IPC_STRUCT_MEMBER(bool, force_keyframe)
IPC_STRUCT_END()

IPC_STRUCT_BEGIN(AcceleratedVideoEncoderMsg_Encode_Params2)
  IPC_STRUCT_MEMBER(int32_t, frame_id)
  IPC_STRUCT_MEMBER(base::TimeDelta, timestamp)
  IPC_STRUCT_MEMBER(std::vector<gfx::GpuMemoryBufferHandle>,
                    gpu_memory_buffer_handles)
  IPC_STRUCT_MEMBER(gfx::Size, size)
  IPC_STRUCT_MEMBER(bool, force_keyframe)
IPC_STRUCT_END()

IPC_STRUCT_TRAITS_BEGIN(media::VideoDecodeAccelerator::Config)
  IPC_STRUCT_TRAITS_MEMBER(profile)
  IPC_STRUCT_TRAITS_MEMBER(is_encrypted)
  IPC_STRUCT_TRAITS_MEMBER(surface_id)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::VideoDecodeAcceleratorSupportedProfile)
  IPC_STRUCT_TRAITS_MEMBER(profile)
  IPC_STRUCT_TRAITS_MEMBER(max_resolution)
  IPC_STRUCT_TRAITS_MEMBER(min_resolution)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::VideoDecodeAcceleratorCapabilities)
  IPC_STRUCT_TRAITS_MEMBER(supported_profiles)
  IPC_STRUCT_TRAITS_MEMBER(flags)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::VideoEncodeAcceleratorSupportedProfile)
  IPC_STRUCT_TRAITS_MEMBER(profile)
  IPC_STRUCT_TRAITS_MEMBER(max_resolution)
  IPC_STRUCT_TRAITS_MEMBER(max_framerate_numerator)
  IPC_STRUCT_TRAITS_MEMBER(max_framerate_denominator)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(media::SubsampleEntry)
  IPC_STRUCT_TRAITS_MEMBER(clear_bytes)
  IPC_STRUCT_TRAITS_MEMBER(cypher_bytes)
IPC_STRUCT_TRAITS_END()

// Create and initialize a hardware video decoder using the specified route_id.
// Created decoders should be freed with AcceleratedVideoDecoderMsg_Destroy when
// no longer needed.
IPC_SYNC_MESSAGE_ROUTED2_1(GpuCommandBufferMsg_CreateVideoDecoder,
                           media::VideoDecodeAccelerator::Config, /* config */
                           int32_t,                               /* route_id */
                           bool /* succeeded */)

// Create and initialize a hardware video encoder using the specified route_id.
// Created encoders should be freed with AcceleratedVideoEncoderMsg_Destroy when
// no longer needed.
IPC_SYNC_MESSAGE_ROUTED5_1(GpuCommandBufferMsg_CreateVideoEncoder,
                           media::VideoPixelFormat /* input_format */,
                           gfx::Size /* input_visible_size */,
                           media::VideoCodecProfile /* output_profile */,
                           uint32_t /* initial_bitrate */,
                           int32_t, /* route_id */
                           bool /* succeeded */)

//------------------------------------------------------------------------------
// Accelerated Video Decoder Messages
// These messages are sent from Renderer process to GPU process.

// Set a CDM on the decoder to handle encrypted buffers.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderMsg_SetCdm, int32_t /* CDM ID */)

// Send input buffer for decoding.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderMsg_Decode,
                    AcceleratedVideoDecoderMsg_Decode_Params)

// Give the texture IDs for the textures the decoder will use for output.
IPC_MESSAGE_ROUTED2(AcceleratedVideoDecoderMsg_AssignPictureBuffers,
                    std::vector<int32_t>,  /* Picture buffer ID */
                    std::vector<uint32_t>) /* Texture ID */

// Send from Renderer process to the GPU process to recycle the given picture
// buffer for further decoding.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderMsg_ReusePictureBuffer,
                    int32_t) /* Picture buffer ID */

// Send flush request to the decoder.
IPC_MESSAGE_ROUTED0(AcceleratedVideoDecoderMsg_Flush)

// Send reset request to the decoder.
IPC_MESSAGE_ROUTED0(AcceleratedVideoDecoderMsg_Reset)

// Send destroy request to the decoder.
IPC_MESSAGE_ROUTED0(AcceleratedVideoDecoderMsg_Destroy)

//------------------------------------------------------------------------------
// Accelerated Video Decoder Host Messages
// These messages are sent from GPU process to Renderer process.
// Inform AcceleratedVideoDecoderHost that AcceleratedVideoDecoder has been
// created.

// Notify the CDM setting result.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderHostMsg_CdmAttached,
                    bool) /* success */

// Accelerated video decoder has consumed input buffer from transfer buffer.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderHostMsg_BitstreamBufferProcessed,
                    int32_t) /* Processed buffer ID */

// Allocate video frames for output of the hardware video decoder.
IPC_MESSAGE_ROUTED3(AcceleratedVideoDecoderHostMsg_ProvidePictureBuffers,
                    int32_t,   /* Number of video frames to generate */
                    gfx::Size, /* Requested size of buffer */
                    uint32_t)  /* Texture target */

// Decoder reports that a picture is ready and buffer does not need to be passed
// back to the decoder.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderHostMsg_DismissPictureBuffer,
                    int32_t) /* Picture buffer ID */

// Decoder reports that a picture is ready.
IPC_MESSAGE_ROUTED4(AcceleratedVideoDecoderHostMsg_PictureReady,
                    int32_t,   /* Picture buffer ID */
                    int32_t,   /* Bitstream buffer ID */
                    gfx::Rect, /* Visible rectangle */
                    bool)      /* Buffer is HW overlay capable */

// Confirm decoder has been flushed.
IPC_MESSAGE_ROUTED0(AcceleratedVideoDecoderHostMsg_FlushDone)

// Confirm decoder has been reset.
IPC_MESSAGE_ROUTED0(AcceleratedVideoDecoderHostMsg_ResetDone)

// Video decoder has encountered an error.
IPC_MESSAGE_ROUTED1(AcceleratedVideoDecoderHostMsg_ErrorNotification,
                    uint32_t /* Error ID */)

//------------------------------------------------------------------------------
// Accelerated Video Encoder Messages
// These messages are sent from the Renderer process to GPU process.

// Queue a video frame to the encoder to encode. |frame_id| will be returned
// by AcceleratedVideoEncoderHostMsg_NotifyInputDone.
IPC_MESSAGE_ROUTED1(AcceleratedVideoEncoderMsg_Encode,
                    AcceleratedVideoEncoderMsg_Encode_Params)

// Queue a GpuMemoryBuffer backed video frame to the encoder to encode.
// |frame_id| will be returned by
// AcceleratedVideoEncoderHostMsg_NotifyInputDone.
IPC_MESSAGE_ROUTED1(AcceleratedVideoEncoderMsg_Encode2,
                    AcceleratedVideoEncoderMsg_Encode_Params2)

// Queue a buffer to the encoder for use in returning output.  |buffer_id| will
// be returned by AcceleratedVideoEncoderHostMsg_BitstreamBufferReady.
IPC_MESSAGE_ROUTED3(AcceleratedVideoEncoderMsg_UseOutputBitstreamBuffer,
                    int32_t /* buffer_id */,
                    base::SharedMemoryHandle /* buffer_handle */,
                    uint32_t /* buffer_size */)

// Request a runtime encoding parameter change.
IPC_MESSAGE_ROUTED2(AcceleratedVideoEncoderMsg_RequestEncodingParametersChange,
                    uint32_t /* bitrate */,
                    uint32_t /* framerate */)

//------------------------------------------------------------------------------
// Accelerated Video Encoder Host Messages
// These messages are sent from GPU process to Renderer process.

// Notify renderer of the input/output buffer requirements of the encoder.
IPC_MESSAGE_ROUTED3(AcceleratedVideoEncoderHostMsg_RequireBitstreamBuffers,
                    uint32_t /* input_count */,
                    gfx::Size /* input_coded_size */,
                    uint32_t /* output_buffer_size */)

// Notify the renderer that the encoder has finished using an input buffer.
// There is no congruent entry point in the media::VideoEncodeAccelerator
// interface, in VEA this same done condition is indicated by dropping the
// reference to the media::VideoFrame passed to VEA::Encode().
IPC_MESSAGE_ROUTED1(AcceleratedVideoEncoderHostMsg_NotifyInputDone,
                    int32_t /* frame_id */)

// Notify the renderer that an output buffer has been filled with encoded data.
IPC_MESSAGE_ROUTED3(AcceleratedVideoEncoderHostMsg_BitstreamBufferReady,
                    int32_t /* bitstream_buffer_id */,
                    uint32_t /* payload_size */,
                    bool /* key_frame */)

// Report error condition.
IPC_MESSAGE_ROUTED1(AcceleratedVideoEncoderHostMsg_NotifyError,
                    media::VideoEncodeAccelerator::Error /* error */)

// Send destroy request to the encoder.
IPC_MESSAGE_ROUTED0(AcceleratedVideoEncoderMsg_Destroy)

//------------------------------------------------------------------------------
// Accelerated JPEG Decoder Messages
// These messages are sent from the Browser process to GPU process.

// Decode one JPEG image from shared memory |input_buffer_handle| with size
// |input_buffer_size|. The input buffer is associated with |input_buffer_id|
// and the size of JPEG image is |coded_size|. Decoded I420 frame data will
// be put onto shared memory associated with |output_video_frame_handle|
// with size limit |output_buffer_size|.
IPC_MESSAGE_ROUTED1(AcceleratedJpegDecoderMsg_Decode,
                    AcceleratedJpegDecoderMsg_Decode_Params)

// Send destroy request to the decoder.
IPC_MESSAGE_ROUTED0(AcceleratedJpegDecoderMsg_Destroy)

//------------------------------------------------------------------------------
// Accelerated JPEG Decoder Host Messages
// These messages are sent from the GPU process to Browser process.
//
// Report decode status.
IPC_MESSAGE_ROUTED2(AcceleratedJpegDecoderHostMsg_DecodeAck,
                    int32_t, /* bitstream_buffer_id */
                    media::JpegDecodeAccelerator::Error /* error */)
