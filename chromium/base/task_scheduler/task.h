// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_TASK_H_
#define BASE_TASK_SCHEDULER_TASK_H_

#include "base/base_export.h"
#include "base/callback_forward.h"
#include "base/location.h"
#include "base/pending_task.h"
#include "base/task_scheduler/task_traits.h"
#include "base/time/time.h"

namespace base {
namespace internal {

// A task is a unit of work inside the task scheduler. Support for tracing and
// profiling inherited from PendingTask.
struct BASE_EXPORT Task : public PendingTask {
  Task(const tracked_objects::Location& posted_from,
       const Closure& task,
       const TaskTraits& traits);
  ~Task();

  // The TaskTraits of this task.
  const TaskTraits traits;

  // The time at which the task was inserted in its sequence. For an undelayed
  // task, this happens at post time. For a delayed task, this happens some
  // time after the task's delay has expired. If the task hasn't been inserted
  // in a sequence yet, this defaults to a null TimeTicks.
  TimeTicks sequenced_time;
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_TASK_H_
