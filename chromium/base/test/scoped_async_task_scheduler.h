// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_SCOPED_ASYNC_TASK_SCHEDULER_H_
#define BASE_TEST_SCOPED_ASYNC_TASK_SCHEDULER_H_

#include "base/macros.h"

namespace base {

class TaskScheduler;

namespace test {

// Allows usage of the base/task_scheduler/post_task.h API within its scope.
//
// To wait until all tasks posted without a delay have run, use
// TaskScheduler::GetInstance()->FlushForTesting().
//
// Deprecated. Use ScopedTaskEnvironment instead. https://crbug.com/708584
//
// TODO(fdoray): Replace ScopedAsyncTaskScheduler instances by
// ScopedTaskEnvironment.
class ScopedAsyncTaskScheduler {
 public:
  // Registers a single-threaded TaskScheduler.
  ScopedAsyncTaskScheduler();

  // Shuts down and unregisters the TaskScheduler.
  //
  // It is guaranteed that all BLOCK_SHUTDOWN tasks have run when this returns.
  ~ScopedAsyncTaskScheduler();

 private:
  const TaskScheduler* task_scheduler_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(ScopedAsyncTaskScheduler);
};

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_SCOPED_ASYNC_TASK_SCHEDULER_H_
