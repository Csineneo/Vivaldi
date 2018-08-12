// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/lib/connector.h"

#include <stdint.h>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "mojo/public/cpp/bindings/lib/sync_handle_watcher.h"

namespace mojo {
namespace internal {

namespace {

// Similar to base::AutoLock, except that it does nothing if |lock| passed into
// the constructor is null.
class MayAutoLock {
 public:
  explicit MayAutoLock(base::Lock* lock) : lock_(lock) {
    if (lock_)
      lock_->Acquire();
  }

  ~MayAutoLock() {
    if (lock_) {
      lock_->AssertAcquired();
      lock_->Release();
    }
  }

 private:
  base::Lock* lock_;
  DISALLOW_COPY_AND_ASSIGN(MayAutoLock);
};

}  // namespace

// ----------------------------------------------------------------------------

Connector::Connector(ScopedMessagePipeHandle message_pipe,
                     ConnectorConfig config,
                     const MojoAsyncWaiter* waiter)
    : waiter_(waiter),
      message_pipe_(std::move(message_pipe)),
      incoming_receiver_(nullptr),
      async_wait_id_(0),
      error_(false),
      drop_writes_(false),
      enforce_errors_from_incoming_receiver_(true),
      paused_(false),
      lock_(config == MULTI_THREADED_SEND ? new base::Lock : nullptr),
      register_sync_handle_watch_count_(0),
      registered_with_sync_handle_watcher_(false),
      sync_handle_watcher_callback_count_(0),
      weak_factory_(this) {
  // Even though we don't have an incoming receiver, we still want to monitor
  // the message pipe to know if is closed or encounters an error.
  WaitToReadMore();
}

Connector::~Connector() {
  DCHECK(thread_checker_.CalledOnValidThread());

  CancelWait();
}

void Connector::CloseMessagePipe() {
  DCHECK(thread_checker_.CalledOnValidThread());

  CancelWait();
  MayAutoLock locker(lock_.get());
  Close(std::move(message_pipe_));
}

ScopedMessagePipeHandle Connector::PassMessagePipe() {
  DCHECK(thread_checker_.CalledOnValidThread());

  CancelWait();
  MayAutoLock locker(lock_.get());
  return std::move(message_pipe_);
}

void Connector::RaiseError() {
  DCHECK(thread_checker_.CalledOnValidThread());

  HandleError(true, true);
}

bool Connector::WaitForIncomingMessage(MojoDeadline deadline) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (error_)
    return false;

  ResumeIncomingMethodCallProcessing();

  MojoResult rv =
      Wait(message_pipe_.get(), MOJO_HANDLE_SIGNAL_READABLE, deadline, nullptr);
  if (rv == MOJO_RESULT_SHOULD_WAIT || rv == MOJO_RESULT_DEADLINE_EXCEEDED)
    return false;
  if (rv != MOJO_RESULT_OK) {
    // Users that call WaitForIncomingMessage() should expect their code to be
    // re-entered, so we call the error handler synchronously.
    HandleError(rv != MOJO_RESULT_FAILED_PRECONDITION, false);
    return false;
  }
  mojo_ignore_result(ReadSingleMessage(&rv));
  return (rv == MOJO_RESULT_OK);
}

void Connector::PauseIncomingMethodCallProcessing() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (paused_)
    return;

  paused_ = true;
  CancelWait();
}

void Connector::ResumeIncomingMethodCallProcessing() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!paused_)
    return;

  paused_ = false;
  WaitToReadMore();
}

bool Connector::Accept(Message* message) {
  DCHECK(lock_ || thread_checker_.CalledOnValidThread());

  // It shouldn't hurt even if |error_| may be changed by a different thread at
  // the same time. The outcome is that we may write into |message_pipe_| after
  // encountering an error, which should be fine.
  if (error_)
    return false;

  MayAutoLock locker(lock_.get());

  if (!message_pipe_.is_valid() || drop_writes_)
    return true;

  MojoResult rv =
      WriteMessageRaw(message_pipe_.get(),
                      message->data(),
                      message->data_num_bytes(),
                      message->mutable_handles()->empty()
                          ? nullptr
                          : reinterpret_cast<const MojoHandle*>(
                                &message->mutable_handles()->front()),
                      static_cast<uint32_t>(message->mutable_handles()->size()),
                      MOJO_WRITE_MESSAGE_FLAG_NONE);

  switch (rv) {
    case MOJO_RESULT_OK:
      // The handles were successfully transferred, so we don't need the message
      // to track their lifetime any longer.
      message->mutable_handles()->clear();
      break;
    case MOJO_RESULT_FAILED_PRECONDITION:
      // There's no point in continuing to write to this pipe since the other
      // end is gone. Avoid writing any future messages. Hide write failures
      // from the caller since we'd like them to continue consuming any backlog
      // of incoming messages before regarding the message pipe as closed.
      drop_writes_ = true;
      break;
    case MOJO_RESULT_BUSY:
      // We'd get a "busy" result if one of the message's handles is:
      //   - |message_pipe_|'s own handle;
      //   - simultaneously being used on another thread; or
      //   - in a "busy" state that prohibits it from being transferred (e.g.,
      //     a data pipe handle in the middle of a two-phase read/write,
      //     regardless of which thread that two-phase read/write is happening
      //     on).
      // TODO(vtl): I wonder if this should be a |DCHECK()|. (But, until
      // crbug.com/389666, etc. are resolved, this will make tests fail quickly
      // rather than hanging.)
      CHECK(false) << "Race condition or other bug detected";
      return false;
    default:
      // This particular write was rejected, presumably because of bad input.
      // The pipe is not necessarily in a bad state.
      return false;
  }
  return true;
}

bool Connector::RegisterSyncHandleWatch() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (error_)
    return false;

  register_sync_handle_watch_count_++;

  if (!registered_with_sync_handle_watcher_ && !paused_) {
    registered_with_sync_handle_watcher_ =
        SyncHandleWatcher::current()->RegisterHandle(
            message_pipe_.get(), MOJO_HANDLE_SIGNAL_READABLE,
            base::Bind(&Connector::OnSyncHandleWatcherHandleReady,
                       base::Unretained(this)));
  }
  return true;
}

void Connector::UnregisterSyncHandleWatch() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (register_sync_handle_watch_count_ == 0) {
    NOTREACHED();
    return;
  }

  register_sync_handle_watch_count_--;
  if (register_sync_handle_watch_count_ > 0)
    return;

  if (registered_with_sync_handle_watcher_) {
    SyncHandleWatcher::current()->UnregisterHandle(message_pipe_.get());
    registered_with_sync_handle_watcher_ = false;
  }
}

bool Connector::RunSyncHandleWatch(const bool* should_stop) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_GT(register_sync_handle_watch_count_, 0u);

  if (error_)
    return false;

  ResumeIncomingMethodCallProcessing();

  if (!should_stop_sync_handle_watch_)
    should_stop_sync_handle_watch_ = new base::RefCountedData<bool>(false);

  // This object may be destroyed during the WatchAllHandles() call. So we have
  // to preserve the boolean that WatchAllHandles uses.
  scoped_refptr<base::RefCountedData<bool>> preserver =
      should_stop_sync_handle_watch_;
  const bool* should_stop_array[] = {should_stop,
                                     &should_stop_sync_handle_watch_->data};
  return SyncHandleWatcher::current()->WatchAllHandles(should_stop_array, 2);
}

// static
void Connector::CallOnHandleReady(void* closure, MojoResult result) {
  Connector* self = static_cast<Connector*>(closure);
  CHECK(self->async_wait_id_ != 0);
  self->async_wait_id_ = 0;
  self->OnHandleReadyInternal(result);
}

void Connector::OnSyncHandleWatcherHandleReady(MojoResult result) {
  base::WeakPtr<Connector> weak_self(weak_factory_.GetWeakPtr());

  sync_handle_watcher_callback_count_++;
  OnHandleReadyInternal(result);
  // At this point, this object might have been deleted.
  if (weak_self)
    sync_handle_watcher_callback_count_--;
}

void Connector::OnHandleReadyInternal(MojoResult result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (result != MOJO_RESULT_OK) {
    HandleError(result != MOJO_RESULT_FAILED_PRECONDITION, false);
    return;
  }
  ReadAllAvailableMessages();
  // At this point, this object might have been deleted. Return.
}

void Connector::WaitToReadMore() {
  CHECK(!async_wait_id_);
  CHECK(!paused_);
  async_wait_id_ = waiter_->AsyncWait(message_pipe_.get().value(),
                                      MOJO_HANDLE_SIGNAL_READABLE,
                                      MOJO_DEADLINE_INDEFINITE,
                                      &Connector::CallOnHandleReady,
                                      this);

  if (register_sync_handle_watch_count_ > 0 &&
      !registered_with_sync_handle_watcher_) {
    registered_with_sync_handle_watcher_ =
        SyncHandleWatcher::current()->RegisterHandle(
            message_pipe_.get(), MOJO_HANDLE_SIGNAL_READABLE,
            base::Bind(&Connector::OnSyncHandleWatcherHandleReady,
                       base::Unretained(this)));
  }
}

bool Connector::ReadSingleMessage(MojoResult* read_result) {
  CHECK(!paused_);

  bool receiver_result = false;

  // Detect if |this| was destroyed during message dispatch. Allow for the
  // possibility of re-entering ReadMore() through message dispatch.
  base::WeakPtr<Connector> weak_self = weak_factory_.GetWeakPtr();

  Message message;
  const MojoResult rv = ReadMessage(message_pipe_.get(), &message);
  *read_result = rv;

  if (rv == MOJO_RESULT_OK) {
    // Dispatching the message may spin in a nested message loop. To ensure we
    // continue dispatching messages when this happens start listening for
    // messagse now.
    if (!async_wait_id_) {
      // TODO: Need to evaluate the perf impact of this.
      WaitToReadMore();
    }
    receiver_result =
        incoming_receiver_ && incoming_receiver_->Accept(&message);
  }

  if (!weak_self)
    return false;

  if (rv == MOJO_RESULT_SHOULD_WAIT)
    return true;

  if (rv != MOJO_RESULT_OK) {
    HandleError(rv != MOJO_RESULT_FAILED_PRECONDITION, false);
    return false;
  }

  if (enforce_errors_from_incoming_receiver_ && !receiver_result) {
    HandleError(true, false);
    return false;
  }
  return true;
}

void Connector::ReadAllAvailableMessages() {
  while (!error_) {
    MojoResult rv;

    // Return immediately if |this| was destroyed. Do not touch any members!
    if (!ReadSingleMessage(&rv))
      return;

    if (paused_)
      return;

    if (rv == MOJO_RESULT_SHOULD_WAIT) {
      // ReadSingleMessage could end up calling HandleError which resets
      // message_pipe_ to a dummy one that is closed. The old EDK will see the
      // that the peer is closed immediately, while the new one is asynchronous
      // because of thread hops. In that case, there'll still be an async
      // waiter.
      if (!async_wait_id_)
        WaitToReadMore();
      break;
    }
  }
}

void Connector::CancelWait() {
  if (async_wait_id_) {
    waiter_->CancelWait(async_wait_id_);
    async_wait_id_ = 0;
  }

  if (registered_with_sync_handle_watcher_) {
    SyncHandleWatcher::current()->UnregisterHandle(message_pipe_.get());
    registered_with_sync_handle_watcher_ = false;
  }

  if (should_stop_sync_handle_watch_)
    should_stop_sync_handle_watch_->data = true;
}

void Connector::HandleError(bool force_pipe_reset, bool force_async_handler) {
  if (error_ || !message_pipe_.is_valid())
    return;

  if (!force_pipe_reset && force_async_handler)
    force_pipe_reset = true;

  if (paused_) {
    // If the user has paused receiving messages, we shouldn't call the error
    // handler right away. We need to wait until the user starts receiving
    // messages again.
    force_async_handler = true;
  }

  if (force_pipe_reset) {
    CancelWait();
    MayAutoLock locker(lock_.get());
    Close(std::move(message_pipe_));
    MessagePipe dummy_pipe;
    message_pipe_ = std::move(dummy_pipe.handle0);
  } else {
    CancelWait();
  }

  if (force_async_handler) {
    // |dummy_pipe.handle1| has been destructed. Reading the pipe will
    // eventually cause a read error on |message_pipe_| and set error state.
    if (!paused_)
      WaitToReadMore();
  } else {
    error_ = true;
    connection_error_handler_.Run();
  }
}

}  // namespace internal
}  // namespace mojo
