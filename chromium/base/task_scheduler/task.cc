// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/task.h"

namespace base {
namespace internal {

Task::Task(const tracked_objects::Location& posted_from,
           const Closure& task,
           const TaskTraits& traits)
    : PendingTask(posted_from,
                  task,
                  TimeTicks(),  // No delayed run time.
                  false),       // Not nestable.
      traits(traits) {}

Task::~Task() = default;

}  // namespace internal
}  // namespace base
