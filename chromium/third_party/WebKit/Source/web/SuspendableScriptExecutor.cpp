// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/SuspendableScriptExecutor.h"

#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/V8PersistentValueVector.h"
#include "core/dom/Document.h"
#include "core/frame/LocalFrame.h"
#include "platform/UserGestureIndicator.h"
#include "public/platform/WebVector.h"
#include "public/web/WebScriptExecutionCallback.h"
#include "wtf/PtrUtil.h"
#include "wtf/Vector.h"
#include <memory>

namespace blink {

namespace {

class WebScriptExecutor : public SuspendableScriptExecutor::Executor {
 public:
  WebScriptExecutor(const HeapVector<ScriptSourceCode>& sources,
                    int worldID,
                    int extensionGroup,
                    bool userGesture);

  Vector<v8::Local<v8::Value>> execute(LocalFrame*) override;

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->trace(m_sources);
    SuspendableScriptExecutor::Executor::trace(visitor);
  }

 private:
  HeapVector<ScriptSourceCode> m_sources;
  int m_worldID;
  int m_extensionGroup;
  bool m_userGesture;
};

WebScriptExecutor::WebScriptExecutor(
    const HeapVector<ScriptSourceCode>& sources,
    int worldID,
    int extensionGroup,
    bool userGesture)
    : m_sources(sources),
      m_worldID(worldID),
      m_extensionGroup(extensionGroup),
      m_userGesture(userGesture) {}

Vector<v8::Local<v8::Value>> WebScriptExecutor::execute(LocalFrame* frame) {
  std::unique_ptr<UserGestureIndicator> indicator;
  if (m_userGesture) {
    indicator = wrapUnique(
        new UserGestureIndicator(DefinitelyProcessingNewUserGesture));
  }

  Vector<v8::Local<v8::Value>> results;
  if (m_worldID) {
    frame->script().executeScriptInIsolatedWorld(m_worldID, m_sources,
                                                 m_extensionGroup, &results);
  } else {
    v8::Local<v8::Value> scriptValue =
        frame->script().executeScriptInMainWorldAndReturnValue(
            m_sources.first());
    results.append(scriptValue);
  }

  return results;
}

class V8FunctionExecutor : public SuspendableScriptExecutor::Executor {
 public:
  V8FunctionExecutor(v8::Isolate*,
                     ScriptState*,
                     v8::Local<v8::Function>,
                     v8::Local<v8::Value> receiver,
                     int argc,
                     v8::Local<v8::Value> argv[]);

  Vector<v8::Local<v8::Value>> execute(LocalFrame*) override;

 private:
  ScopedPersistent<v8::Function> m_function;
  ScopedPersistent<v8::Value> m_receiver;
  V8PersistentValueVector<v8::Value> m_args;
  RefPtr<ScriptState> m_scriptState;
};

V8FunctionExecutor::V8FunctionExecutor(v8::Isolate* isolate,
                                       ScriptState* scriptState,
                                       v8::Local<v8::Function> function,
                                       v8::Local<v8::Value> receiver,
                                       int argc,
                                       v8::Local<v8::Value> argv[])
    : m_function(isolate, function),
      m_receiver(isolate, receiver),
      m_args(isolate),
      m_scriptState(scriptState) {
  m_args.ReserveCapacity(argc);
  for (int i = 0; i < argc; ++i)
    m_args.Append(argv[i]);
}

Vector<v8::Local<v8::Value>> V8FunctionExecutor::execute(LocalFrame* frame) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  Vector<v8::Local<v8::Value>> results;
  if (!m_scriptState->contextIsValid())
    return results;
  ScriptState::Scope scope(m_scriptState.get());
  v8::Local<v8::Value> singleResult;
  Vector<v8::Local<v8::Value>> args;
  args.reserveCapacity(m_args.Size());
  for (size_t i = 0; i < m_args.Size(); ++i)
    args.append(m_args.Get(i));
  if (V8ScriptRunner::callFunction(m_function.newLocal(isolate),
                                   frame->document(),
                                   m_receiver.newLocal(isolate), args.size(),
                                   args.data(), toIsolate(frame))
          .ToLocal(&singleResult))
    results.append(singleResult);
  return results;
}

}  // namespace

void SuspendableScriptExecutor::createAndRun(
    LocalFrame* frame,
    int worldID,
    const HeapVector<ScriptSourceCode>& sources,
    int extensionGroup,
    bool userGesture,
    WebScriptExecutionCallback* callback) {
  SuspendableScriptExecutor* executor = new SuspendableScriptExecutor(
      frame, callback,
      new WebScriptExecutor(sources, worldID, extensionGroup, userGesture));
  executor->run();
}

void SuspendableScriptExecutor::createAndRun(
    LocalFrame* frame,
    v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Function> function,
    v8::Local<v8::Value> receiver,
    int argc,
    v8::Local<v8::Value> argv[],
    WebScriptExecutionCallback* callback) {
  ScriptState* scriptState = ScriptState::from(context);
  if (!scriptState->contextIsValid()) {
    if (callback)
      callback->completed(Vector<v8::Local<v8::Value>>());
    return;
  }
  SuspendableScriptExecutor* executor = new SuspendableScriptExecutor(
      frame, callback, new V8FunctionExecutor(isolate, scriptState, function,
                                              receiver, argc, argv));
  executor->run();
}

void SuspendableScriptExecutor::contextDestroyed() {
  SuspendableTimer::contextDestroyed();
  if (m_callback)
    m_callback->completed(Vector<v8::Local<v8::Value>>());
  dispose();
}

SuspendableScriptExecutor::SuspendableScriptExecutor(
    LocalFrame* frame,
    WebScriptExecutionCallback* callback,
    Executor* executor)
    : SuspendableTimer(frame->document()),
      m_frame(frame),
      m_callback(callback),
      m_keepAlive(this),
      m_executor(executor) {}

SuspendableScriptExecutor::~SuspendableScriptExecutor() {}

void SuspendableScriptExecutor::fired() {
  executeAndDestroySelf();
}

void SuspendableScriptExecutor::run() {
  ExecutionContext* context = getExecutionContext();
  DCHECK(context);
  if (!context->activeDOMObjectsAreSuspended()) {
    suspendIfNeeded();
    executeAndDestroySelf();
    return;
  }
  startOneShot(0, BLINK_FROM_HERE);
  suspendIfNeeded();
}

void SuspendableScriptExecutor::executeAndDestroySelf() {
  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Vector<v8::Local<v8::Value>> results = m_executor->execute(m_frame);

  // The script may have removed the frame, in which case contextDestroyed()
  // will have handled the disposal/callback.
  if (!m_frame->client())
    return;

  if (m_callback)
    m_callback->completed(results);
  dispose();
}

void SuspendableScriptExecutor::dispose() {
  // Remove object as a ContextLifecycleObserver.
  ActiveDOMObject::clearContext();
  m_keepAlive.clear();
  stop();
}

DEFINE_TRACE(SuspendableScriptExecutor) {
  visitor->trace(m_frame);
  visitor->trace(m_executor);
  SuspendableTimer::trace(visitor);
}

}  // namespace blink
