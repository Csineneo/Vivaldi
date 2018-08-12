// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/video_capture_buffer_pool_impl.h"

#include <memory>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "build/build_config.h"
#include "media/capture/video/video_capture_buffer_handle.h"
#include "media/capture/video/video_capture_buffer_tracker.h"
#include "ui/gfx/buffer_format_util.h"

namespace media {

VideoCaptureBufferPoolImpl::VideoCaptureBufferPoolImpl(
    std::unique_ptr<VideoCaptureBufferTrackerFactory> buffer_tracker_factory,
    int count)
    : count_(count),
      next_buffer_id_(0),
      last_relinquished_buffer_id_(kInvalidId),
      buffer_tracker_factory_(std::move(buffer_tracker_factory)) {
  DCHECK_GT(count, 0);
}

VideoCaptureBufferPoolImpl::~VideoCaptureBufferPoolImpl() {
  base::STLDeleteValues(&trackers_);
}

bool VideoCaptureBufferPoolImpl::ShareToProcess(
    int buffer_id,
    base::ProcessHandle process_handle,
    base::SharedMemoryHandle* new_handle) {
  base::AutoLock lock(lock_);

  VideoCaptureBufferTracker* tracker = GetTracker(buffer_id);
  if (!tracker) {
    NOTREACHED() << "Invalid buffer_id.";
    return false;
  }
  if (tracker->ShareToProcess(process_handle, new_handle))
    return true;
  DPLOG(ERROR) << "Error mapping memory";
  return false;
}

bool VideoCaptureBufferPoolImpl::ShareToProcess2(
    int buffer_id,
    int plane,
    base::ProcessHandle process_handle,
    gfx::GpuMemoryBufferHandle* new_handle) {
  base::AutoLock lock(lock_);

  VideoCaptureBufferTracker* tracker = GetTracker(buffer_id);
  if (!tracker) {
    NOTREACHED() << "Invalid buffer_id.";
    return false;
  }
  if (tracker->ShareToProcess2(plane, process_handle, new_handle))
    return true;
  DPLOG(ERROR) << "Error mapping memory";
  return false;
}

std::unique_ptr<VideoCaptureBufferHandle>
VideoCaptureBufferPoolImpl::GetBufferHandle(int buffer_id) {
  base::AutoLock lock(lock_);

  VideoCaptureBufferTracker* tracker = GetTracker(buffer_id);
  if (!tracker) {
    NOTREACHED() << "Invalid buffer_id.";
    return std::unique_ptr<VideoCaptureBufferHandle>();
  }

  DCHECK(tracker->held_by_producer());
  return tracker->GetBufferHandle();
}

int VideoCaptureBufferPoolImpl::ReserveForProducer(
    const gfx::Size& dimensions,
    media::VideoPixelFormat format,
    media::VideoPixelStorage storage,
    int* buffer_id_to_drop) {
  base::AutoLock lock(lock_);
  return ReserveForProducerInternal(dimensions, format, storage,
                                    buffer_id_to_drop);
}

void VideoCaptureBufferPoolImpl::RelinquishProducerReservation(int buffer_id) {
  base::AutoLock lock(lock_);
  VideoCaptureBufferTracker* tracker = GetTracker(buffer_id);
  if (!tracker) {
    NOTREACHED() << "Invalid buffer_id.";
    return;
  }
  DCHECK(tracker->held_by_producer());
  tracker->set_held_by_producer(false);
  last_relinquished_buffer_id_ = buffer_id;
}

void VideoCaptureBufferPoolImpl::HoldForConsumers(int buffer_id,
                                                  int num_clients) {
  base::AutoLock lock(lock_);
  VideoCaptureBufferTracker* tracker = GetTracker(buffer_id);
  if (!tracker) {
    NOTREACHED() << "Invalid buffer_id.";
    return;
  }
  DCHECK(tracker->held_by_producer());
  DCHECK(!tracker->consumer_hold_count());

  tracker->set_consumer_hold_count(num_clients);
  // Note: |held_by_producer()| will stay true until
  // RelinquishProducerReservation() (usually called by destructor of the object
  // wrapping this tracker, e.g. a media::VideoFrame).
}

void VideoCaptureBufferPoolImpl::RelinquishConsumerHold(int buffer_id,
                                                        int num_clients) {
  base::AutoLock lock(lock_);
  VideoCaptureBufferTracker* tracker = GetTracker(buffer_id);
  if (!tracker) {
    NOTREACHED() << "Invalid buffer_id.";
    return;
  }
  DCHECK_GE(tracker->consumer_hold_count(), num_clients);

  tracker->set_consumer_hold_count(tracker->consumer_hold_count() -
                                   num_clients);
}

int VideoCaptureBufferPoolImpl::ResurrectLastForProducer(
    const gfx::Size& dimensions,
    media::VideoPixelFormat format,
    media::VideoPixelStorage storage) {
  base::AutoLock lock(lock_);

  // Return early if the last relinquished buffer has been re-used already.
  if (last_relinquished_buffer_id_ == kInvalidId)
    return kInvalidId;

  // If there are no consumers reading from this buffer, then it's safe to
  // provide this buffer back to the producer (because the producer may
  // potentially modify the content). Check that the expected dimensions,
  // format, and storage match.
  TrackerMap::iterator it = trackers_.find(last_relinquished_buffer_id_);
  DCHECK(it != trackers_.end());
  DCHECK(!it->second->held_by_producer());
  if (it->second->consumer_hold_count() == 0 &&
      it->second->dimensions() == dimensions &&
      it->second->pixel_format() == format &&
      it->second->storage_type() == storage) {
    it->second->set_held_by_producer(true);
    const int resurrected_buffer_id = last_relinquished_buffer_id_;
    last_relinquished_buffer_id_ = kInvalidId;
    return resurrected_buffer_id;
  }

  return kInvalidId;
}

double VideoCaptureBufferPoolImpl::GetBufferPoolUtilization() const {
  base::AutoLock lock(lock_);
  int num_buffers_held = 0;
  for (const auto& entry : trackers_) {
    VideoCaptureBufferTracker* const tracker = entry.second;
    if (tracker->held_by_producer() || tracker->consumer_hold_count() > 0)
      ++num_buffers_held;
  }
  return static_cast<double>(num_buffers_held) / count_;
}

int VideoCaptureBufferPoolImpl::ReserveForProducerInternal(
    const gfx::Size& dimensions,
    media::VideoPixelFormat pixel_format,
    media::VideoPixelStorage storage_type,
    int* buffer_id_to_drop) {
  lock_.AssertAcquired();

  const size_t size_in_pixels = dimensions.GetArea();
  // Look for a tracker that's allocated, big enough, and not in use. Track the
  // largest one that's not big enough, in case we have to reallocate a tracker.
  *buffer_id_to_drop = kInvalidId;
  size_t largest_size_in_pixels = 0;
  TrackerMap::iterator tracker_of_last_resort = trackers_.end();
  TrackerMap::iterator tracker_to_drop = trackers_.end();
  for (TrackerMap::iterator it = trackers_.begin(); it != trackers_.end();
       ++it) {
    VideoCaptureBufferTracker* const tracker = it->second;
    if (!tracker->consumer_hold_count() && !tracker->held_by_producer()) {
      if (tracker->max_pixel_count() >= size_in_pixels &&
          (tracker->pixel_format() == pixel_format) &&
          (tracker->storage_type() == storage_type)) {
        if (it->first == last_relinquished_buffer_id_) {
          // This buffer would do just fine, but avoid returning it because the
          // client may want to resurrect it. It will be returned perforce if
          // the pool has reached it's maximum limit (see code below).
          tracker_of_last_resort = it;
          continue;
        }
        // Existing tracker is big enough and has correct format. Reuse it.
        tracker->set_dimensions(dimensions);
        tracker->set_held_by_producer(true);
        return it->first;
      }
      if (tracker->max_pixel_count() > largest_size_in_pixels) {
        largest_size_in_pixels = tracker->max_pixel_count();
        tracker_to_drop = it;
      }
    }
  }

  // Preferably grow the pool by creating a new tracker. If we're at maximum
  // size, then try using |tracker_of_last_resort| or reallocate by deleting an
  // existing one instead.
  if (trackers_.size() == static_cast<size_t>(count_)) {
    if (tracker_of_last_resort != trackers_.end()) {
      last_relinquished_buffer_id_ = kInvalidId;
      tracker_of_last_resort->second->set_dimensions(dimensions);
      tracker_of_last_resort->second->set_held_by_producer(true);
      return tracker_of_last_resort->first;
    }
    if (tracker_to_drop == trackers_.end()) {
      // We're out of space, and can't find an unused tracker to reallocate.
      return kInvalidId;
    }
    if (tracker_to_drop->first == last_relinquished_buffer_id_)
      last_relinquished_buffer_id_ = kInvalidId;
    *buffer_id_to_drop = tracker_to_drop->first;
    delete tracker_to_drop->second;
    trackers_.erase(tracker_to_drop);
  }

  // Create the new tracker.
  const int buffer_id = next_buffer_id_++;

  std::unique_ptr<VideoCaptureBufferTracker> tracker =
      buffer_tracker_factory_->CreateTracker(storage_type);
  // TODO(emircan): We pass the lock here to solve GMB allocation issue, see
  // crbug.com/545238.
  if (!tracker->Init(dimensions, pixel_format, storage_type, &lock_)) {
    DLOG(ERROR) << "Error initializing VideoCaptureBufferTracker";
    return kInvalidId;
  }

  tracker->set_held_by_producer(true);
  trackers_[buffer_id] = tracker.release();

  return buffer_id;
}

VideoCaptureBufferTracker* VideoCaptureBufferPoolImpl::GetTracker(
    int buffer_id) {
  TrackerMap::const_iterator it = trackers_.find(buffer_id);
  return (it == trackers_.end()) ? NULL : it->second;
}

}  // namespace media
