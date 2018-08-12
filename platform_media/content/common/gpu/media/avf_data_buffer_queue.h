// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA.

#ifndef CONTENT_COMMON_GPU_MEDIA_AVF_DATA_BUFFER_QUEUE_H_
#define CONTENT_COMMON_GPU_MEDIA_AVF_DATA_BUFFER_QUEUE_H_

#include <string>

#include "base/callback.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "content/common/content_export.h"

namespace media {
class DataBuffer;
}

namespace content {

class CONTENT_EXPORT AVFDataBufferQueue {
 public:
  // Used for debugging only.
  enum Type { AUDIO, VIDEO };

  using ReadCB = base::Callback<void(const scoped_refptr<media::DataBuffer>&)>;

  AVFDataBufferQueue(Type type,
                     const base::TimeDelta& capacity,
                     const base::Closure& capacity_available_cb,
                     const base::Closure& capacity_depleted_cb);
  ~AVFDataBufferQueue();

  void Read(const ReadCB& read_cb);

  void BufferReady(const scoped_refptr<media::DataBuffer>& buffer);

  void SetEndOfStream();

  void Flush();

  bool HasAvailableCapacity() const;

  size_t memory_usage() const;

 private:
  class Queue;

  std::string DescribeBufferSize() const;
  void SatisfyPendingRead();

  const Type type_;
  const base::TimeDelta capacity_;
  base::Closure capacity_available_cb_;
  base::Closure capacity_depleted_cb_;
  ReadCB read_cb_;
  std::unique_ptr<Queue> buffer_queue_;

  // We are "catching up" if the stream associated with this queue lags behind
  // another stream.  This is when we want to allow the queue to return any
  // buffers it currently has as quickly as possible.
  bool catching_up_;

  bool end_of_stream_;

  base::ThreadChecker thread_checker_;
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_AVF_DATA_BUFFER_QUEUE_H_
