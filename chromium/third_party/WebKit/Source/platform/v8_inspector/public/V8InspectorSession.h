// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8InspectorSession_h
#define V8InspectorSession_h

#include "platform/PlatformExport.h"
#include "platform/inspector_protocol/TypeBuilder.h"
#include "wtf/PassOwnPtr.h"

#include <v8.h>

namespace blink {

class V8DebuggerAgent;
class V8HeapProfilerAgent;
class V8InspectorSessionClient;
class V8ProfilerAgent;
class V8RuntimeAgent;

class PLATFORM_EXPORT V8InspectorSession {
public:
    static const char backtraceObjectGroup[];

    // Cross-context inspectable values (DOM nodes in different worlds, etc.).
    class Inspectable {
    public:
        virtual v8::Local<v8::Value> get(v8::Local<v8::Context>) = 0;
        virtual ~Inspectable() { }
    };

    virtual ~V8InspectorSession() { }

    virtual void setClient(V8InspectorSessionClient*) = 0;
    virtual void addInspectedObject(PassOwnPtr<Inspectable>) = 0;

    // API for the embedder to report native activities.
    virtual void schedulePauseOnNextStatement(const String16& breakReason, PassOwnPtr<protocol::DictionaryValue> data) = 0;
    virtual void cancelPauseOnNextStatement() = 0;
    virtual void breakProgram(const String16& breakReason, PassOwnPtr<protocol::DictionaryValue> data) = 0;
    virtual void breakProgramOnException(const String16& breakReason, PassOwnPtr<protocol::DictionaryValue> data) = 0;
    virtual void setSkipAllPauses(bool) = 0;

    // API to report async call stacks.
    virtual void asyncTaskScheduled(const String16& taskName, void* task, bool recurring) = 0;
    virtual void asyncTaskCanceled(void* task) = 0;
    virtual void asyncTaskStarted(void* task) = 0;
    virtual void asyncTaskFinished(void* task) = 0;
    virtual void allAsyncTasksCanceled() = 0;

    // API to work with remote objects.
    virtual PassOwnPtr<protocol::Runtime::RemoteObject> wrapObject(v8::Local<v8::Context>, v8::Local<v8::Value>, const String16& groupName, bool generatePreview = false) = 0;
    // FIXME: remove when InspectorConsoleAgent moves into V8 inspector.
    virtual PassOwnPtr<protocol::Runtime::RemoteObject> wrapTable(v8::Local<v8::Context>, v8::Local<v8::Value> table, v8::Local<v8::Value> columns) = 0;
    virtual v8::Local<v8::Value> findObject(ErrorString*, const String16& objectId, v8::Local<v8::Context>* = nullptr, String16* objectGroup = nullptr) = 0;
    virtual void releaseObjectGroup(const String16&) = 0;

    virtual V8DebuggerAgent* debuggerAgent() = 0;
    virtual V8HeapProfilerAgent* heapProfilerAgent() = 0;
    virtual V8ProfilerAgent* profilerAgent() = 0;
    virtual V8RuntimeAgent* runtimeAgent() = 0;
};

} // namespace blink

#endif // V8InspectorSession_h
