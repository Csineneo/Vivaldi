// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/scheduler/renderer/task_queue_throttler.h"

#include <cstdint>

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/strings/stringprintf.h"
#include "platform/scheduler/base/real_time_domain.h"
#include "platform/scheduler/child/scheduler_tqm_delegate.h"
#include "platform/scheduler/renderer/auto_advancing_virtual_time_domain.h"
#include "platform/scheduler/renderer/renderer_scheduler_impl.h"
#include "platform/scheduler/renderer/throttled_time_domain.h"
#include "platform/scheduler/renderer/web_frame_scheduler_impl.h"
#include "public/platform/WebFrameScheduler.h"

namespace blink {
namespace scheduler {

namespace {
const int kMaxBudgetLevelInSeconds = 1;

base::Optional<base::TimeTicks> NextTaskRunTime(LazyNow* lazy_now,
                                                TaskQueue* queue) {
  if (queue->HasPendingImmediateWork())
    return lazy_now->Now();
  return queue->GetNextScheduledWakeUp();
}

template <class T>
T Min(const base::Optional<T>& optional, const T& value) {
  if (!optional) {
    return value;
  }
  return std::min(optional.value(), value);
}

template <class T>
base::Optional<T> Min(const base::Optional<T>& a, const base::Optional<T>& b) {
  if (!b)
    return a;
  if (!a)
    return b;
  return std::min(a.value(), b.value());
}

template <class T>
T Max(const base::Optional<T>& optional, const T& value) {
  if (!optional)
    return value;
  return std::max(optional.value(), value);
}

template <class T>
base::Optional<T> Max(const base::Optional<T>& a, const base::Optional<T>& b) {
  if (!b)
    return a;
  if (!a)
    return b;
  return std::max(a.value(), b.value());
}

}  // namespace

TaskQueueThrottler::TimeBudgetPool::TimeBudgetPool(
    const char* name,
    TaskQueueThrottler* task_queue_throttler,
    base::TimeTicks now)
    : name_(name),
      task_queue_throttler_(task_queue_throttler),
      max_budget_level_(base::TimeDelta::FromSeconds(kMaxBudgetLevelInSeconds)),
      last_checkpoint_(now),
      cpu_percentage_(1),
      is_enabled_(true) {}

TaskQueueThrottler::TimeBudgetPool::~TimeBudgetPool() {}

void TaskQueueThrottler::TimeBudgetPool::SetTimeBudget(base::TimeTicks now,
                                                       double cpu_percentage) {
  Advance(now);
  cpu_percentage_ = cpu_percentage;
}

void TaskQueueThrottler::TimeBudgetPool::AddQueue(base::TimeTicks now,
                                                  TaskQueue* queue) {
  Metadata& metadata = task_queue_throttler_->queue_details_[queue];
  DCHECK(!metadata.time_budget_pool);
  metadata.time_budget_pool = this;

  associated_task_queues_.insert(queue);

  if (!metadata.IsThrottled())
    return;

  queue->SetQueueEnabled(false);

  task_queue_throttler_->MaybeSchedulePumpQueue(FROM_HERE, now, queue,
                                                GetNextAllowedRunTime());
}

void TaskQueueThrottler::TimeBudgetPool::RemoveQueue(base::TimeTicks now,
                                                     TaskQueue* queue) {
  auto find_it = task_queue_throttler_->queue_details_.find(queue);
  DCHECK(find_it != task_queue_throttler_->queue_details_.end() &&
         find_it->second.time_budget_pool == this);
  find_it->second.time_budget_pool = nullptr;
  bool is_throttled = find_it->second.IsThrottled();

  task_queue_throttler_->MaybeDeleteQueueMetadata(find_it);
  associated_task_queues_.erase(queue);

  if (is_throttled)
    return;

  task_queue_throttler_->MaybeSchedulePumpQueue(FROM_HERE, now, queue,
                                                base::nullopt);
}

void TaskQueueThrottler::TimeBudgetPool::EnableThrottling(LazyNow* lazy_now) {
  if (is_enabled_)
    return;
  is_enabled_ = true;

  BlockThrottledQueues(lazy_now->Now());
}

void TaskQueueThrottler::TimeBudgetPool::DisableThrottling(LazyNow* lazy_now) {
  if (!is_enabled_)
    return;
  is_enabled_ = false;

  for (TaskQueue* queue : associated_task_queues_) {
    if (!task_queue_throttler_->IsThrottled(queue))
      continue;

    task_queue_throttler_->MaybeSchedulePumpQueue(FROM_HERE, lazy_now->Now(),
                                                  queue, base::nullopt);
  }
}

bool TaskQueueThrottler::TimeBudgetPool::IsThrottlingEnabled() const {
  return is_enabled_;
}

void TaskQueueThrottler::TimeBudgetPool::Close() {
  DCHECK_EQ(0u, associated_task_queues_.size());

  task_queue_throttler_->time_budget_pools_.erase(this);
}

bool TaskQueueThrottler::TimeBudgetPool::HasEnoughBudgetToRun(
    base::TimeTicks now) {
  Advance(now);
  return !is_enabled_ || current_budget_level_.InMicroseconds() >= 0;
}

base::TimeTicks TaskQueueThrottler::TimeBudgetPool::GetNextAllowedRunTime() {
  if (!is_enabled_ || current_budget_level_.InMicroseconds() >= 0) {
    return last_checkpoint_;
  } else {
    // Subtract because current_budget is negative.
    return last_checkpoint_ - current_budget_level_ / cpu_percentage_;
  }
}

void TaskQueueThrottler::TimeBudgetPool::RecordTaskRunTime(
    base::TimeDelta task_run_time) {
  if (is_enabled_)
    current_budget_level_ -= task_run_time;
}

const char* TaskQueueThrottler::TimeBudgetPool::Name() const {
  return name_;
}

void TaskQueueThrottler::TimeBudgetPool::AsValueInto(
    base::trace_event::TracedValue* state,
    base::TimeTicks now) const {
  state->BeginDictionary();

  state->SetString("name", name_);
  state->SetDouble("time_budget", cpu_percentage_);
  state->SetDouble("time_budget_level_in_seconds",
                   current_budget_level_.InSecondsF());
  state->SetDouble("last_checkpoint_seconds_ago",
                   (now - last_checkpoint_).InSecondsF());

  state->BeginArray("task_queues");
  for (TaskQueue* queue : associated_task_queues_) {
    state->AppendString(base::StringPrintf(
        "%" PRIx64, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(queue))));
  }
  state->EndArray();

  state->EndDictionary();
}

void TaskQueueThrottler::TimeBudgetPool::Advance(base::TimeTicks now) {
  if (now > last_checkpoint_) {
    if (is_enabled_) {
      current_budget_level_ = std::min(
          current_budget_level_ + cpu_percentage_ * (now - last_checkpoint_),
          max_budget_level_);
    }
    last_checkpoint_ = now;
  }
}

void TaskQueueThrottler::TimeBudgetPool::BlockThrottledQueues(
    base::TimeTicks now) {
  for (TaskQueue* queue : associated_task_queues_) {
    if (!task_queue_throttler_->IsThrottled(queue))
      continue;

    queue->SetQueueEnabled(false);
    task_queue_throttler_->MaybeSchedulePumpQueue(FROM_HERE, now, queue,
                                                  base::nullopt);
  }
}

TaskQueueThrottler::TaskQueueThrottler(
    RendererSchedulerImpl* renderer_scheduler,
    const char* tracing_category)
    : task_runner_(renderer_scheduler->ControlTaskRunner()),
      renderer_scheduler_(renderer_scheduler),
      tick_clock_(renderer_scheduler->tick_clock()),
      tracing_category_(tracing_category),
      time_domain_(new ThrottledTimeDomain(this, tracing_category)),
      virtual_time_(false),
      weak_factory_(this) {
  pump_throttled_tasks_closure_.Reset(base::Bind(
      &TaskQueueThrottler::PumpThrottledTasks, weak_factory_.GetWeakPtr()));
  forward_immediate_work_callback_ =
      base::Bind(&TaskQueueThrottler::OnTimeDomainHasImmediateWork,
                 weak_factory_.GetWeakPtr());

  renderer_scheduler_->RegisterTimeDomain(time_domain_.get());
}

TaskQueueThrottler::~TaskQueueThrottler() {
  // It's possible for queues to be still throttled, so we need to tidy up
  // before unregistering the time domain.
  for (const TaskQueueMap::value_type& map_entry : queue_details_) {
    if (map_entry.second.IsThrottled()) {
      TaskQueue* task_queue = map_entry.first;
      task_queue->SetTimeDomain(renderer_scheduler_->real_time_domain());
      task_queue->RemoveFence();
    }
  }

  renderer_scheduler_->UnregisterTimeDomain(time_domain_.get());
}

void TaskQueueThrottler::SetQueueEnabled(TaskQueue* task_queue, bool enabled) {
  TaskQueueMap::iterator find_it = queue_details_.find(task_queue);

  if (find_it == queue_details_.end()) {
    task_queue->SetQueueEnabled(enabled);
    return;
  }

  find_it->second.enabled = enabled;

  if (!find_it->second.IsThrottled())
    return;

  // We don't enable the queue here because it's throttled and there might be
  // tasks in it's work queue that would execute immediatly rather than after
  // PumpThrottledTasks runs.
  if (!enabled) {
    task_queue->SetQueueEnabled(false);
    MaybeSchedulePumpQueue(FROM_HERE, tick_clock_->NowTicks(), task_queue,
                           base::nullopt);
  }
}

void TaskQueueThrottler::IncreaseThrottleRefCount(TaskQueue* task_queue) {
  DCHECK_NE(task_queue, task_runner_.get());

  if (virtual_time_)
    return;

  std::pair<TaskQueueMap::iterator, bool> insert_result =
      queue_details_.insert(std::make_pair(task_queue, Metadata()));

  if (!insert_result.first->second.IsThrottled()) {
    // The insert was successful so we need to throttle the queue.
    insert_result.first->second.enabled = task_queue->IsQueueEnabled();

    task_queue->SetTimeDomain(time_domain_.get());
    task_queue->RemoveFence();
    task_queue->SetQueueEnabled(false);

    if (!task_queue->IsEmpty()) {
      if (task_queue->HasPendingImmediateWork()) {
        OnTimeDomainHasImmediateWork(task_queue);
      } else {
        OnTimeDomainHasDelayedWork(task_queue);
      }
    }

    TRACE_EVENT1(tracing_category_, "TaskQueueThrottler_TaskQueueThrottled",
                 "task_queue", task_queue);
  }

  insert_result.first->second.throttling_ref_count++;
}

void TaskQueueThrottler::DecreaseThrottleRefCount(TaskQueue* task_queue) {
  if (virtual_time_)
    return;

  TaskQueueMap::iterator iter = queue_details_.find(task_queue);

  if (iter != queue_details_.end() &&
      --iter->second.throttling_ref_count == 0) {
    bool enabled = iter->second.enabled;

    MaybeDeleteQueueMetadata(iter);

    task_queue->SetTimeDomain(renderer_scheduler_->real_time_domain());
    task_queue->RemoveFence();
    task_queue->SetQueueEnabled(enabled);

    TRACE_EVENT1(tracing_category_, "TaskQueueThrottler_TaskQueueUntrottled",
                 "task_queue", task_queue);
  }
}

bool TaskQueueThrottler::IsThrottled(TaskQueue* task_queue) const {
  auto find_it = queue_details_.find(task_queue);
  if (find_it == queue_details_.end())
    return false;
  return find_it->second.IsThrottled();
}

void TaskQueueThrottler::UnregisterTaskQueue(TaskQueue* task_queue) {
  LazyNow lazy_now(tick_clock_);
  auto find_it = queue_details_.find(task_queue);

  if (find_it == queue_details_.end())
    return;

  if (find_it->second.time_budget_pool)
    find_it->second.time_budget_pool->RemoveQueue(lazy_now.Now(), task_queue);

  queue_details_.erase(find_it);
}

void TaskQueueThrottler::OnTimeDomainHasImmediateWork(TaskQueue* queue) {
  // Forward to the main thread if called from another thread
  if (!task_runner_->RunsTasksOnCurrentThread()) {
    task_runner_->PostTask(FROM_HERE,
                           base::Bind(forward_immediate_work_callback_, queue));
    return;
  }
  TRACE_EVENT0(tracing_category_,
               "TaskQueueThrottler::OnTimeDomainHasImmediateWork");

  base::TimeTicks now = tick_clock_->NowTicks();
  base::TimeTicks next_allowed_run_time = GetNextAllowedRunTime(now, queue);
  MaybeSchedulePumpThrottledTasks(FROM_HERE, now, next_allowed_run_time);
}

void TaskQueueThrottler::OnTimeDomainHasDelayedWork(TaskQueue* queue) {
  TRACE_EVENT0(tracing_category_,
               "TaskQueueThrottler::OnTimeDomainHasDelayedWork");
  base::TimeTicks now = tick_clock_->NowTicks();
  LazyNow lazy_now(now);

  base::Optional<base::TimeTicks> next_scheduled_delayed_task =
      NextTaskRunTime(&lazy_now, queue);
  DCHECK(next_scheduled_delayed_task);
  MaybeSchedulePumpThrottledTasks(FROM_HERE, now,
                                  next_scheduled_delayed_task.value());
}

void TaskQueueThrottler::PumpThrottledTasks() {
  TRACE_EVENT0(tracing_category_, "TaskQueueThrottler::PumpThrottledTasks");
  pending_pump_throttled_tasks_runtime_.reset();

  LazyNow lazy_now(tick_clock_);
  base::Optional<base::TimeTicks> next_scheduled_delayed_task;

  for (const TaskQueueMap::value_type& map_entry : queue_details_) {
    TaskQueue* task_queue = map_entry.first;
    if (!map_entry.second.enabled || task_queue->IsEmpty() ||
        !map_entry.second.IsThrottled())
      continue;

    // Don't enable queues whose budget pool doesn't allow them to run now.
    base::TimeTicks next_allowed_run_time =
        GetNextAllowedRunTime(lazy_now.Now(), task_queue);
    base::Optional<base::TimeTicks> next_desired_run_time =
        NextTaskRunTime(&lazy_now, task_queue);

    if (next_desired_run_time &&
        next_allowed_run_time > next_desired_run_time.value()) {
      TRACE_EVENT1(
          tracing_category_,
          "TaskQueueThrottler::PumpThrottledTasks_ExpensiveTaskThrottled",
          "throttle_time_in_seconds",
          (next_allowed_run_time - next_desired_run_time.value()).InSecondsF());

      // Schedule a pump for queue which was disabled because of time budget.
      next_scheduled_delayed_task =
          Min(next_scheduled_delayed_task, next_allowed_run_time);

      continue;
    }

    next_scheduled_delayed_task =
        Min(next_scheduled_delayed_task, task_queue->GetNextScheduledWakeUp());

    if (next_allowed_run_time > lazy_now.Now())
      continue;

    task_queue->SetQueueEnabled(true);
    task_queue->InsertFence();
  }

  // Maybe schedule a call to TaskQueueThrottler::PumpThrottledTasks if there is
  // a pending delayed task or a throttled task ready to run.
  // NOTE: posting a non-delayed task in the future will result in
  // TaskQueueThrottler::OnTimeDomainHasImmediateWork being called.
  if (next_scheduled_delayed_task) {
    MaybeSchedulePumpThrottledTasks(FROM_HERE, lazy_now.Now(),
                                    *next_scheduled_delayed_task);
  }
}

/* static */
base::TimeTicks TaskQueueThrottler::AlignedThrottledRunTime(
    base::TimeTicks unthrottled_runtime) {
  const base::TimeDelta one_second = base::TimeDelta::FromSeconds(1);
  return unthrottled_runtime + one_second -
         ((unthrottled_runtime - base::TimeTicks()) % one_second);
}

void TaskQueueThrottler::MaybeSchedulePumpThrottledTasks(
    const tracked_objects::Location& from_here,
    base::TimeTicks now,
    base::TimeTicks unaligned_runtime) {
  if (virtual_time_)
    return;

  base::TimeTicks runtime =
      std::max(now, AlignedThrottledRunTime(unaligned_runtime));

  // If there is a pending call to PumpThrottledTasks and it's sooner than
  // |runtime| then return.
  if (pending_pump_throttled_tasks_runtime_ &&
      runtime >= pending_pump_throttled_tasks_runtime_.value()) {
    return;
  }

  pending_pump_throttled_tasks_runtime_ = runtime;

  pump_throttled_tasks_closure_.Cancel();

  base::TimeDelta delay = pending_pump_throttled_tasks_runtime_.value() - now;
  TRACE_EVENT1(tracing_category_,
               "TaskQueueThrottler::MaybeSchedulePumpThrottledTasks",
               "delay_till_next_pump_ms", delay.InMilliseconds());
  task_runner_->PostDelayedTask(
      from_here, pump_throttled_tasks_closure_.callback(), delay);
}

void TaskQueueThrottler::EnableVirtualTime() {
  virtual_time_ = true;

  pump_throttled_tasks_closure_.Cancel();

  for (auto it = queue_details_.begin(); it != queue_details_.end();) {
    TaskQueue* task_queue = it->first;
    bool enabled = it->second.enabled;

    if (!it->second.time_budget_pool) {
      it = queue_details_.erase(it);
    } else {
      // Fall back to default values.
      it->second.throttling_ref_count = 0;
      it->second.enabled = false;
      it++;
    }

    task_queue->SetTimeDomain(renderer_scheduler_->GetVirtualTimeDomain());
    task_queue->RemoveFence();
    task_queue->SetQueueEnabled(enabled);
  }
}

TaskQueueThrottler::TimeBudgetPool* TaskQueueThrottler::CreateTimeBudgetPool(
    const char* name) {
  TimeBudgetPool* time_budget_pool =
      new TimeBudgetPool(name, this, tick_clock_->NowTicks());
  time_budget_pools_[time_budget_pool] = base::WrapUnique(time_budget_pool);
  return time_budget_pool;
}

void TaskQueueThrottler::OnTaskRunTimeReported(TaskQueue* task_queue,
                                               base::TimeTicks start_time,
                                               base::TimeTicks end_time) {
  if (!IsThrottled(task_queue))
    return;

  TimeBudgetPool* time_budget_pool = GetTimeBudgetPoolForQueue(task_queue);
  if (!time_budget_pool)
    return;

  time_budget_pool->RecordTaskRunTime(end_time - start_time);
  if (!time_budget_pool->HasEnoughBudgetToRun(end_time))
    time_budget_pool->BlockThrottledQueues(end_time);
}

void TaskQueueThrottler::AsValueInto(base::trace_event::TracedValue* state,
                                     base::TimeTicks now) const {
  if (pending_pump_throttled_tasks_runtime_) {
    state->SetDouble(
        "next_throttled_tasks_pump_in_seconds",
        (pending_pump_throttled_tasks_runtime_.value() - now).InSecondsF());
  }

  state->BeginDictionary("time_budget_pools");

  for (const auto& map_entry : time_budget_pools_) {
    TaskQueueThrottler::TimeBudgetPool* pool = map_entry.first;
    pool->AsValueInto(state, now);
  }

  state->EndDictionary();
}

TaskQueueThrottler::TimeBudgetPool*
TaskQueueThrottler::GetTimeBudgetPoolForQueue(TaskQueue* queue) {
  auto find_it = queue_details_.find(queue);
  if (find_it == queue_details_.end())
    return nullptr;
  return find_it->second.time_budget_pool;
}

void TaskQueueThrottler::MaybeSchedulePumpQueue(
    const tracked_objects::Location& from_here,
    base::TimeTicks now,
    TaskQueue* queue,
    base::Optional<base::TimeTicks> next_possible_run_time) {
  LazyNow lazy_now(now);
  base::Optional<base::TimeTicks> next_run_time =
      Max(NextTaskRunTime(&lazy_now, queue), next_possible_run_time);

  if (next_run_time) {
    MaybeSchedulePumpThrottledTasks(from_here, now, next_run_time.value());
  }
}

base::TimeTicks TaskQueueThrottler::GetNextAllowedRunTime(base::TimeTicks now,
                                                          TaskQueue* queue) {
  TimeBudgetPool* time_budget_pool = GetTimeBudgetPoolForQueue(queue);
  if (!time_budget_pool)
    return now;
  return std::max(now, time_budget_pool->GetNextAllowedRunTime());
}

void TaskQueueThrottler::MaybeDeleteQueueMetadata(TaskQueueMap::iterator it) {
  if (!it->second.IsThrottled() && !it->second.time_budget_pool)
    queue_details_.erase(it);
}

}  // namespace scheduler
}  // namespace blink
