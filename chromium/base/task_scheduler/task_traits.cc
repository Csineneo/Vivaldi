// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/task_traits.h"

namespace base {

// Do not rely on defaults hard-coded below beyond the guarantees described in
// the header; anything else is subject to change. Tasks should explicitly
// request defaults if the behavior is critical to the task.
TaskTraits::TaskTraits()
    : with_file_io_(false),
      priority_(TaskPriority::BACKGROUND),
      shutdown_behavior_(TaskShutdownBehavior::BLOCK_SHUTDOWN) {}

TaskTraits::~TaskTraits() = default;

TaskTraits& TaskTraits::WithFileIO() {
  with_file_io_ = true;
  return *this;
}

TaskTraits& TaskTraits::WithPriority(TaskPriority priority) {
  priority_ = priority;
  return *this;
}

TaskTraits& TaskTraits::WithShutdownBehavior(
    TaskShutdownBehavior shutdown_behavior) {
  shutdown_behavior_ = shutdown_behavior;
  return *this;
}

}  // namespace base
