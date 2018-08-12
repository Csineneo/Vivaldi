// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA.

#include "media/filters/pass_through_audio_decoder.h"

namespace media {

PassThroughAudioDecoder::PassThroughAudioDecoder(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : impl_(task_runner) {
}

PassThroughAudioDecoder::~PassThroughAudioDecoder() {
}

void PassThroughAudioDecoder::Initialize(const AudioDecoderConfig& config,
                                         CdmContext* cdm_context,
                                         const InitCB& init_cb,
                                         const OutputCB& output_cb) {
  impl_.Initialize(config, init_cb, output_cb);
}

void PassThroughAudioDecoder::Decode(const scoped_refptr<DecoderBuffer>& buffer,
                                     const DecodeCB& decode_cb) {
  impl_.Decode(buffer, decode_cb);
}

void PassThroughAudioDecoder::Reset(const base::Closure& closure) {
  impl_.Reset(closure);
}

std::string PassThroughAudioDecoder::GetDisplayName() const {
  return "PassThroughAudioDecoder";
}

}  // namespace media
