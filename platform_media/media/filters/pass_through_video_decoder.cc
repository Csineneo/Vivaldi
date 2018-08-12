// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#include "media/filters/pass_through_video_decoder.h"

namespace media {

PassThroughVideoDecoder::PassThroughVideoDecoder(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : impl_(task_runner) {
}

PassThroughVideoDecoder::~PassThroughVideoDecoder() {
}

void PassThroughVideoDecoder::Initialize(const VideoDecoderConfig& config,
                                         bool low_delay,
                                         CdmContext* cdm_context,
                                         const InitCB& init_cb,
                                         const OutputCB& output_cb) {
  impl_.Initialize(config, init_cb, output_cb);
}

void PassThroughVideoDecoder::Decode(
    const scoped_refptr<DecoderBuffer>& buffer,
    const DecodeCB& decode_cb) {
  impl_.Decode(buffer, decode_cb);
}

void PassThroughVideoDecoder::Reset(const base::Closure& closure) {
  impl_.Reset(closure);
}

std::string PassThroughVideoDecoder::GetDisplayName() const {
  return "PassThroughVideoDecoder";
}

}  // namespace media
