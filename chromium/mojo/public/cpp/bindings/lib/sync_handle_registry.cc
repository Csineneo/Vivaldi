// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/sync_handle_registry.h"

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/threading/thread_local.h"
#include "mojo/public/c/system/core.h"

namespace mojo {
namespace {

base::LazyInstance<base::ThreadLocalPointer<SyncHandleRegistry>>::Leaky
    g_current_sync_handle_watcher = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
scoped_refptr<SyncHandleRegistry> SyncHandleRegistry::current() {
  scoped_refptr<SyncHandleRegistry> result(
      g_current_sync_handle_watcher.Pointer()->Get());
  if (!result) {
    result = new SyncHandleRegistry();
    DCHECK_EQ(result.get(), g_current_sync_handle_watcher.Pointer()->Get());
  }
  return result;
}

bool SyncHandleRegistry::RegisterHandle(const Handle& handle,
                                        MojoHandleSignals handle_signals,
                                        const HandleCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (base::ContainsKey(handles_, handle))
    return false;

  MojoResult result = wait_set_.AddHandle(handle, handle_signals);
  if (result != MOJO_RESULT_OK)
    return false;

  handles_[handle] = callback;
  return true;
}

void SyncHandleRegistry::UnregisterHandle(const Handle& handle) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!base::ContainsKey(handles_, handle))
    return;

  MojoResult result = wait_set_.RemoveHandle(handle);
  DCHECK_EQ(MOJO_RESULT_OK, result);
  handles_.erase(handle);
}

bool SyncHandleRegistry::RegisterEvent(base::WaitableEvent* event,
                                       const base::Closure& callback) {
  auto result = events_.insert({event, callback});
  DCHECK(result.second);
  MojoResult rv = wait_set_.AddEvent(event);
  if (rv == MOJO_RESULT_OK)
    return true;
  DCHECK_EQ(MOJO_RESULT_ALREADY_EXISTS, rv);
  return false;
}

void SyncHandleRegistry::UnregisterEvent(base::WaitableEvent* event) {
  auto it = events_.find(event);
  DCHECK(it != events_.end());
  events_.erase(it);
  MojoResult rv = wait_set_.RemoveEvent(event);
  DCHECK_EQ(MOJO_RESULT_OK, rv);
}

bool SyncHandleRegistry::Wait(const bool* should_stop[], size_t count) {
  DCHECK(thread_checker_.CalledOnValidThread());

  size_t num_ready_handles;
  Handle ready_handle;
  MojoResult ready_handle_result;

  scoped_refptr<SyncHandleRegistry> preserver(this);
  while (true) {
    for (size_t i = 0; i < count; ++i)
      if (*should_stop[i])
        return true;

    // TODO(yzshen): Theoretically it can reduce sync call re-entrancy if we
    // give priority to the handle that is waiting for sync response.
    base::WaitableEvent* ready_event = nullptr;
    num_ready_handles = 1;
    wait_set_.Wait(&ready_event, &num_ready_handles, &ready_handle,
                   &ready_handle_result);
    if (num_ready_handles) {
      DCHECK_EQ(1u, num_ready_handles);
      const auto iter = handles_.find(ready_handle);
      iter->second.Run(ready_handle_result);
    }

    if (ready_event) {
      const auto iter = events_.find(ready_event);
      DCHECK(iter != events_.end());
      iter->second.Run();
    }
  };

  return false;
}

SyncHandleRegistry::SyncHandleRegistry() {
  DCHECK(!g_current_sync_handle_watcher.Pointer()->Get());
  g_current_sync_handle_watcher.Pointer()->Set(this);
}

SyncHandleRegistry::~SyncHandleRegistry() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // This object may be destructed after the thread local storage slot used by
  // |g_current_sync_handle_watcher| is reset during thread shutdown.
  // For example, another slot in the thread local storage holds a referrence to
  // this object, and that slot is cleaned up after
  // |g_current_sync_handle_watcher|.
  if (!g_current_sync_handle_watcher.Pointer()->Get())
    return;

  // If this breaks, it is likely that the global variable is bulit into and
  // accessed from multiple modules.
  DCHECK_EQ(this, g_current_sync_handle_watcher.Pointer()->Get());

  g_current_sync_handle_watcher.Pointer()->Set(nullptr);
}

}  // namespace mojo
