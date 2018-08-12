// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_WEBKIT_PUBLIC_PLATFORM_SCHEDULER_BASE_TASK_QUEUE_H_
#define THIRD_PARTY_WEBKIT_PUBLIC_PLATFORM_SCHEDULER_BASE_TASK_QUEUE_H_

#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/optional.h"
#include "base/single_thread_task_runner.h"
#include "base/time/time.h"
#include "public/platform/WebCommon.h"

namespace base {
namespace trace_event {
class BlameContext;
}
}

namespace blink {
namespace scheduler {
namespace internal {
class TaskQueueImpl;
}  // namespace internal
class FakeWebTaskRunner;
class LazyNow;
class TimeDomain;

class BLINK_PLATFORM_EXPORT TaskQueue : public base::SingleThreadTaskRunner {
 public:
  TaskQueue() {}

  // Unregisters the task queue after which no tasks posted to it will run and
  // the TaskQueueManager's reference to it will be released soon.
  virtual void UnregisterTaskQueue() = 0;

  enum QueuePriority {
    // Queues with control priority will run before any other queue, and will
    // explicitly starve other queues. Typically this should only be used for
    // private queues which perform control operations.
    CONTROL_PRIORITY,
    // Queues with high priority will be selected preferentially over normal or
    // best effort queues. The selector will ensure that high priority queues
    // cannot completely starve normal priority queues.
    HIGH_PRIORITY,
    // Queues with normal priority are the default.
    NORMAL_PRIORITY,
    // Queues with best effort priority will only be run if all other queues are
    // empty. They can be starved by the other queues.
    BEST_EFFORT_PRIORITY,
    // Must be the last entry.
    QUEUE_PRIORITY_COUNT,
    FIRST_QUEUE_PRIORITY = CONTROL_PRIORITY,
  };

  // Options for constructing a TaskQueue. Once set the |name| and
  // |should_monitor_quiescence| are immutable.
  struct Spec {
    // Note |name| must have application lifetime.
    explicit Spec(const char* name)
        : name(name),
          should_monitor_quiescence(false),
          time_domain(nullptr),
          should_notify_observers(true),
          should_report_when_execution_blocked(false) {}

    Spec SetShouldMonitorQuiescence(bool should_monitor) {
      should_monitor_quiescence = should_monitor;
      return *this;
    }

    Spec SetShouldNotifyObservers(bool run_observers) {
      should_notify_observers = run_observers;
      return *this;
    }

    Spec SetTimeDomain(TimeDomain* domain) {
      time_domain = domain;
      return *this;
    }

    // See TaskQueueManager::Observer::OnTriedToExecuteBlockedTask.
    Spec SetShouldReportWhenExecutionBlocked(bool should_report) {
      should_report_when_execution_blocked = should_report;
      return *this;
    }

    const char* name;
    bool should_monitor_quiescence;
    TimeDomain* time_domain;
    bool should_notify_observers;
    bool should_report_when_execution_blocked;
  };

  // Enable or disable task execution for this queue. NOTE this must be called
  // on the thread this TaskQueue was created by.
  virtual void SetQueueEnabled(bool enabled) = 0;

  // NOTE this must be called on the thread this TaskQueue was created by.
  virtual bool IsQueueEnabled() const = 0;

  // Returns true if the queue is completely empty.
  virtual bool IsEmpty() const = 0;

  // Returns true if the queue has work that's ready to execute now.
  // NOTE: this must be called on the thread this TaskQueue was created by.
  virtual bool HasPendingImmediateWork() const = 0;

  // Returns requested run time of next delayed task, which is not ready
  // to run. If there are no such tasks, returns base::nullopt.
  // NOTE: this must be called on the thread this TaskQueue was created by.
  virtual base::Optional<base::TimeTicks> GetNextScheduledWakeUp() = 0;

  // Can be called on any thread.
  virtual const char* GetName() const = 0;

  // Set the priority of the queue to |priority|. NOTE this must be called on
  // the thread this TaskQueue was created by.
  virtual void SetQueuePriority(QueuePriority priority) = 0;

  // Returns the current queue priority.
  virtual QueuePriority GetQueuePriority() const = 0;

  // These functions can only be called on the same thread that the task queue
  // manager executes its tasks on.
  virtual void AddTaskObserver(
      base::MessageLoop::TaskObserver* task_observer) = 0;
  virtual void RemoveTaskObserver(
      base::MessageLoop::TaskObserver* task_observer) = 0;

  // Set the blame context which is entered and left while executing tasks from
  // this task queue. |blame_context| must be null or outlive this task queue.
  // Must be called on the thread this TaskQueue was created by.
  virtual void SetBlameContext(
      base::trace_event::BlameContext* blame_context) = 0;

  // Removes the task queue from the previous TimeDomain and adds it to
  // |domain|.  This is a moderately expensive operation.
  virtual void SetTimeDomain(TimeDomain* domain) = 0;

  // Returns the queue's current TimeDomain.  Can be called from any thread.
  virtual TimeDomain* GetTimeDomain() const = 0;

  // Inserts a barrier into the task queue which inhibits non-delayed tasks
  // posted after this point, or delayed tasks which are not yet ready to run,
  // from being executed until the fence is cleared.  If a fence already existed
  // the one supersedes it and previously blocked tasks will now run up until
  // the new fence is hit.
  virtual void InsertFence() = 0;

  // Removes any previously added fence and unblocks execution of any tasks
  // blocked by it.
  virtual void RemoveFence() = 0;

  virtual bool BlockedByFence() const = 0;

 protected:
  ~TaskQueue() override {}

  DISALLOW_COPY_AND_ASSIGN(TaskQueue);
};

}  // namespace scheduler
}  // namespace blink

#endif  // THIRD_PARTY_WEBKIT_PUBLIC_PLATFORM_SCHEDULER_BASE_TASK_QUEUE_H_
