// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#ifndef MEDIA_FILTERS_PLATFORM_MEDIA_PIPELINE_TYPES_H_
#define MEDIA_FILTERS_PLATFORM_MEDIA_PIPELINE_TYPES_H_

#include "media/base/sample_format.h"
#include "media/base/video_frame.h"
#include "media/base/video_rotation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace media {

// Order is important, be careful when adding new values.
enum PlatformMediaDataType {
  PLATFORM_MEDIA_AUDIO,
  PLATFORM_MEDIA_VIDEO,
  PLATFORM_MEDIA_DATA_TYPE_COUNT  // Always keep this as the last one.
};

enum MediaDataStatus {
  kOk,
  kEOS,
  kError,
  kConfigChanged,
  kMediaDataStatusCount,
};

// Order is important, be careful when adding new values.
enum class PlatformMediaDecodingMode {
  SOFTWARE,
  HARDWARE,
  COUNT  // Always keep this as the last one.
};

struct PlatformMediaTimeInfo {
  base::TimeDelta duration;
  base::TimeDelta start_time;
};

struct PlatformAudioConfig {
  PlatformAudioConfig()
      : format(kUnknownSampleFormat),
        channel_count(-1),
        samples_per_second(-1) {}

  bool is_valid() const {
    return channel_count > 0 && samples_per_second > 0 &&
           format != kUnknownSampleFormat;
  }

  media::SampleFormat format;
  int channel_count;
  int samples_per_second;
};

struct MEDIA_EXPORT PlatformVideoConfig {
  struct Plane {
    Plane() : stride(-1), offset(-1) {}

    bool is_valid() const { return stride > 0 && offset >= 0 && size > 0; }

    int stride;
    int offset;
    int size;
  };

  PlatformVideoConfig();
  PlatformVideoConfig(const PlatformVideoConfig& other);
  ~PlatformVideoConfig();

  bool is_valid() const {
    return !coded_size.IsEmpty() && !visible_rect.IsEmpty() &&
           !natural_size.IsEmpty() && planes[VideoFrame::kYPlane].is_valid() &&
           planes[VideoFrame::kVPlane].is_valid() &&
           planes[VideoFrame::kUPlane].is_valid() &&
           decoding_mode < PlatformMediaDecodingMode::COUNT;
  }

  gfx::Size coded_size;
  gfx::Rect visible_rect;
  gfx::Size natural_size;
  Plane planes[VideoFrame::kMaxPlanes];
  VideoRotation rotation;
  PlatformMediaDecodingMode decoding_mode;
};

}  // namespace media

#endif  // MEDIA_FILTERS_PLATFORM_MEDIA_PIPELINE_TYPES_H_
