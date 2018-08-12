/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WorkerLoaderProxy_h
#define WorkerLoaderProxy_h

#include "core/CoreExport.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/ThreadSafeRefCounted.h"
#include "platform/wtf/ThreadingPrimitives.h"
#include "public/platform/WebTraceLocation.h"

namespace blink {

class ThreadableLoadingContext;

// The WorkerLoaderProxy is a proxy to the loader context. Normally, the
// document on the main thread provides loading services for the subordinate
// workers. WorkerLoaderProxy provides 2-way communications to the Document
// context and back to the worker.
//
// Note that in multi-process browsers, the Worker object context and the
// Document context can be distinct.

// The abstract interface providing the methods for actually posting tasks;
// separated from the thread-safe & ref-counted WorkerLoaderProxy object which
// keeps a protected reference to the provider object. This to support
// non-overlapping lifetimes, the provider may be destructed before all
// references to the WorkerLoaderProxy object have been dropped.
//
// A provider implementation must detach itself when finalizing by calling
// WorkerLoaderProxy::detachProvider(). This stops the WorkerLoaderProxy from
// accessing the now-dead object, but it will remain alive while ref-ptrs are
// still kept to it.
class CORE_EXPORT WorkerLoaderProxyProvider {
 public:
  virtual ~WorkerLoaderProxyProvider() {}

  // Posts a task to the thread which runs the loading code (normally, the main
  // thread). This must be called from a worker thread.
  virtual void PostTaskToLoader(const WebTraceLocation&,
                                std::unique_ptr<WTF::CrossThreadClosure>) = 0;

  // Posts callbacks from loading code to the WorkerGlobalScope. This must be
  // called from the main thread.
  virtual void PostTaskToWorkerGlobalScope(
      const WebTraceLocation&,
      std::unique_ptr<WTF::CrossThreadClosure>) = 0;

  // It is guaranteed that this gets accessed only on the thread where
  // the loading context is bound.
  virtual ThreadableLoadingContext* GetThreadableLoadingContext() = 0;
};

class CORE_EXPORT WorkerLoaderProxy final
    : public ThreadSafeRefCounted<WorkerLoaderProxy> {
 public:
  static PassRefPtr<WorkerLoaderProxy> Create(
      WorkerLoaderProxyProvider* loader_proxy_provider) {
    return AdoptRef(new WorkerLoaderProxy(loader_proxy_provider));
  }

  ~WorkerLoaderProxy();

  // This must be called from a worker thread.
  void PostTaskToLoader(const WebTraceLocation&,
                        std::unique_ptr<WTF::CrossThreadClosure>);

  // This must be called from the main thread.
  void PostTaskToWorkerGlobalScope(const WebTraceLocation&,
                                   std::unique_ptr<WTF::CrossThreadClosure>);

  // This may return nullptr.
  // This must be called from the main thread (== the thread of the
  // loading context).
  ThreadableLoadingContext* GetThreadableLoadingContext();

  // Notification from the provider that it can no longer be accessed. An
  // implementation of WorkerLoaderProxyProvider is required to call
  // detachProvider() when finalizing. This must be called from the main thread.
  void DetachProvider(WorkerLoaderProxyProvider*);

 private:
  explicit WorkerLoaderProxy(WorkerLoaderProxyProvider*);

  WTF::Mutex lock_;
  WorkerLoaderProxyProvider* loader_proxy_provider_;
};

}  // namespace blink

#endif  // WorkerLoaderProxy_h
