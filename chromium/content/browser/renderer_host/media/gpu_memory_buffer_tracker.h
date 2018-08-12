// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_GPU_MEMORY_BUFFER_TRACKER_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_GPU_MEMORY_BUFFER_TRACKER_H_

#include "media/capture/video/video_capture_buffer_tracker.h"

namespace content {

// Tracker specifics for GpuMemoryBuffer. Owns GpuMemoryBuffers and its
// associated pixel dimensions.
class GpuMemoryBufferTracker final : public media::VideoCaptureBufferTracker {
 public:
  GpuMemoryBufferTracker();
  ~GpuMemoryBufferTracker() override;

  bool Init(const gfx::Size& dimensions,
            media::VideoPixelFormat format,
            media::VideoPixelStorage storage_type,
            base::Lock* lock) override;

  std::unique_ptr<media::VideoCaptureBufferHandle> GetBufferHandle() override;

  bool ShareToProcess(base::ProcessHandle process_handle,
                      base::SharedMemoryHandle* new_handle) override;

  bool ShareToProcess2(int plane,
                       base::ProcessHandle process_handle,
                       gfx::GpuMemoryBufferHandle* new_handle) override;

 private:
  friend class GpuMemoryBufferBufferHandle;

  // Owned references to GpuMemoryBuffers.
  std::vector<std::unique_ptr<gfx::GpuMemoryBuffer>> gpu_memory_buffers_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_GPU_MEMORY_BUFFER_TRACKER_H_
