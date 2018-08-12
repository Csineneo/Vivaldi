// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/priority_queue.h"

#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"

namespace base {
namespace internal {

PriorityQueue::SequenceAndSortKey::SequenceAndSortKey()
    : sort_key(TaskPriority::LOWEST, TimeTicks()) {}

PriorityQueue::SequenceAndSortKey::SequenceAndSortKey(
    scoped_refptr<Sequence> sequence,
    const SequenceSortKey& sort_key)
    : sequence(std::move(sequence)), sort_key(sort_key) {}

PriorityQueue::SequenceAndSortKey::~SequenceAndSortKey() = default;

PriorityQueue::Transaction::Transaction(PriorityQueue* outer_queue)
    : auto_lock_(new AutoSchedulerLock(outer_queue->container_lock_)),
      outer_queue_(outer_queue) {
  DCHECK(CalledOnValidThread());
}

PriorityQueue::Transaction::~Transaction() {
  DCHECK(CalledOnValidThread());

  // Run the wake up callback once for each call to Push(). Perform this outside
  // the scope of PriorityQueue's lock to avoid imposing an unnecessary lock
  // dependency on the callback's destination.
  auto_lock_.reset();
  for (size_t i = 0; i < num_wake_ups_; ++i)
    outer_queue_->wake_up_callback_.Run();
}

void PriorityQueue::Transaction::Push(
    std::unique_ptr<SequenceAndSortKey> sequence_and_sort_key) {
  DCHECK(CalledOnValidThread());
  DCHECK(!sequence_and_sort_key->is_null());

  PushNoWakeUp(std::move(sequence_and_sort_key));

  ++num_wake_ups_;
}

void PriorityQueue::Transaction::PushNoWakeUp(
    std::unique_ptr<SequenceAndSortKey> sequence_and_sort_key) {
  DCHECK(CalledOnValidThread());
  DCHECK(!sequence_and_sort_key->is_null());

  outer_queue_->container_.push(std::move(sequence_and_sort_key));
}

const PriorityQueue::SequenceAndSortKey& PriorityQueue::Transaction::Peek()
    const {
  DCHECK(CalledOnValidThread());

  // TODO(fdoray): Add an IsEmpty() method to Transaction and require Peek() to
  // be called on a non-empty PriorityQueue only.
  if (outer_queue_->container_.empty())
    return outer_queue_->empty_sequence_and_sort_key_;

  return *outer_queue_->container_.top();
}

void PriorityQueue::Transaction::Pop() {
  DCHECK(CalledOnValidThread());
  DCHECK(!outer_queue_->container_.empty());
  outer_queue_->container_.pop();
}

PriorityQueue::PriorityQueue(const Closure& wake_up_callback)
    : wake_up_callback_(wake_up_callback) {
  DCHECK(!wake_up_callback_.is_null());
}

PriorityQueue::PriorityQueue(const Closure& wake_up_callback,
                             const PriorityQueue* predecessor_priority_queue)
    : container_lock_(&predecessor_priority_queue->container_lock_),
      wake_up_callback_(wake_up_callback) {
  DCHECK(!wake_up_callback_.is_null());
  DCHECK(predecessor_priority_queue);
}

PriorityQueue::~PriorityQueue() = default;

std::unique_ptr<PriorityQueue::Transaction> PriorityQueue::BeginTransaction() {
  return WrapUnique(new Transaction(this));
}

}  // namespace internal
}  // namespace base
