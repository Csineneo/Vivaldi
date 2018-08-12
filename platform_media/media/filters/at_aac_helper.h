// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2015 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA.

#ifndef MEDIA_FILTERS_AT_AAC_HELPER_H_
#define MEDIA_FILTERS_AT_AAC_HELPER_H_

#include "media/filters/at_codec_helper.h"

namespace media {

class ATAACHelper final : public ATCodecHelper {
 public:
  ATAACHelper();
  ~ATAACHelper() override;

  bool Initialize(const AudioDecoderConfig& config,
                  const InputFormatKnownCB& input_format_known_cb,
                  const ConvertAudioCB& convert_audio_cb) override;
  bool ProcessBuffer(const scoped_refptr<DecoderBuffer>& buffer) override;

 private:
  class AudioFormatReader;

  bool ReadInputFormat(const scoped_refptr<DecoderBuffer>& buffer);
  bool ConvertAudio(const scoped_refptr<DecoderBuffer>& buffer);

  bool is_input_format_known() const { return input_format_reader_ == nullptr; }

  ConvertAudioCB convert_audio_cb_;
  InputFormatKnownCB input_format_known_cb_;
  ScopedAudioChannelLayoutPtr input_channel_layout_;
  std::unique_ptr<AudioFormatReader> input_format_reader_;

  DISALLOW_COPY_AND_ASSIGN(ATAACHelper);
};

}  // namespace media

#endif  // MEDIA_FILTERS_AT_AAC_HELPER_H_
