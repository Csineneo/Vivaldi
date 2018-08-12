// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/inspector/ThreadDebugger.h"

#include "bindings/core/v8/ScriptCallStack.h"
#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/V8Binding.h"
#include "bindings/core/v8/V8DOMException.h"
#include "bindings/core/v8/V8DOMTokenList.h"
#include "bindings/core/v8/V8HTMLAllCollection.h"
#include "bindings/core/v8/V8HTMLCollection.h"
#include "bindings/core/v8/V8Node.h"
#include "bindings/core/v8/V8NodeList.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/InspectorDOMDebuggerAgent.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/inspector/ScriptArguments.h"
#include "platform/ScriptForbiddenScope.h"
#include "wtf/CurrentTime.h"

namespace blink {

ThreadDebugger::ThreadDebugger(v8::Isolate* isolate)
    : m_isolate(isolate)
    , m_debugger(V8Debugger::create(isolate, this))
{
}

ThreadDebugger::~ThreadDebugger()
{
}

void ThreadDebugger::willExecuteScript(v8::Isolate* isolate, int scriptId)
{
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    if (data && data->threadDebugger())
        data->threadDebugger()->debugger()->willExecuteScript(isolate->GetCurrentContext(), scriptId);
}

void ThreadDebugger::didExecuteScript(v8::Isolate* isolate)
{
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    if (data && data->threadDebugger())
        data->threadDebugger()->debugger()->didExecuteScript(isolate->GetCurrentContext());
}

void ThreadDebugger::idleStarted(v8::Isolate* isolate)
{
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    if (data && data->threadDebugger())
        data->threadDebugger()->debugger()->idleStarted();
}

void ThreadDebugger::idleFinished(v8::Isolate* isolate)
{
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    if (data && data->threadDebugger())
        data->threadDebugger()->debugger()->idleFinished();
}

void ThreadDebugger::beginUserGesture()
{
    m_userGestureIndicator = adoptPtr(new UserGestureIndicator(DefinitelyProcessingNewUserGesture));
}

void ThreadDebugger::endUserGesture()
{
    m_userGestureIndicator.clear();
}

void ThreadDebugger::eventListeners(v8::Local<v8::Value> value, V8EventListenerInfoList& result)
{
    InspectorDOMDebuggerAgent::eventListenersInfoForTarget(m_isolate, value, result);
}

String16 ThreadDebugger::valueSubtype(v8::Local<v8::Value> value)
{
    if (V8Node::hasInstance(value, m_isolate))
        return "node";
    if (V8NodeList::hasInstance(value, m_isolate)
        || V8DOMTokenList::hasInstance(value, m_isolate)
        || V8HTMLCollection::hasInstance(value, m_isolate)
        || V8HTMLAllCollection::hasInstance(value, m_isolate)) {
        return "array";
    }
    if (V8DOMException::hasInstance(value, m_isolate))
        return "error";
    return String();
}

bool ThreadDebugger::formatAccessorsAsProperties(v8::Local<v8::Value> value)
{
    return V8DOMWrapper::isWrapper(m_isolate, value);
}

bool ThreadDebugger::isExecutionAllowed()
{
    return !ScriptForbiddenScope::isScriptForbidden();
}

double ThreadDebugger::currentTimeMS()
{
    return WTF::currentTimeMS();
}

bool ThreadDebugger::isInspectableHeapObject(v8::Local<v8::Object> object)
{
    if (object->InternalFieldCount() < v8DefaultWrapperInternalFieldCount)
        return true;
    v8::Local<v8::Value> wrapper = object->GetInternalField(v8DOMWrapperObjectIndex);
    // Skip wrapper boilerplates which are like regular wrappers but don't have
    // native object.
    if (!wrapper.IsEmpty() && wrapper->IsUndefined())
        return false;
    return true;
}

void ThreadDebugger::reportMessageToConsole(v8::Local<v8::Context> context, MessageType type, MessageLevel level, const String16& message, const v8::FunctionCallbackInfo<v8::Value>* arguments, unsigned skipArgumentCount)
{
    ScriptState* scriptState = ScriptState::from(context);
    ScriptArguments* scriptArguments = nullptr;
    if (arguments && scriptState->contextIsValid())
        scriptArguments = ScriptArguments::create(scriptState, *arguments, skipArgumentCount);
    String messageText = message;
    if (messageText.isEmpty() && scriptArguments)
        scriptArguments->getFirstArgumentAsString(messageText);

    ConsoleMessage* consoleMessage = ConsoleMessage::create(ConsoleAPIMessageSource, level, messageText);
    consoleMessage->setType(type);
    consoleMessage->setScriptState(scriptState);
    if (arguments)
        consoleMessage->setScriptArguments(scriptArguments);
    reportMessageToConsole(context, consoleMessage);
}

void ThreadDebugger::consoleTime(const String16& title)
{
    TRACE_EVENT_COPY_ASYNC_BEGIN0("blink.console", String(title).utf8().data(), this);
}

void ThreadDebugger::consoleTimeEnd(const String16& title)
{
    TRACE_EVENT_COPY_ASYNC_END0("blink.console", String(title).utf8().data(), this);
}

void ThreadDebugger::consoleTimeStamp(const String16& title)
{
    v8::Isolate* isolate = m_isolate;
    TRACE_EVENT_INSTANT1("devtools.timeline", "TimeStamp", TRACE_EVENT_SCOPE_THREAD, "data", InspectorTimeStampEvent::data(currentExecutionContext(isolate), title));
}

void ThreadDebugger::startRepeatingTimer(double interval, V8DebuggerClient::TimerCallback callback, void* data)
{
    m_timerData.append(data);
    m_timerCallbacks.append(callback);

    OwnPtr<Timer<ThreadDebugger>> timer = adoptPtr(new Timer<ThreadDebugger>(this, &ThreadDebugger::onTimer));
    Timer<ThreadDebugger>* timerPtr = timer.get();
    m_timers.append(std::move(timer));
    timerPtr->startRepeating(interval, BLINK_FROM_HERE);
}

void ThreadDebugger::cancelTimer(void* data)
{
    for (size_t index = 0; index < m_timerData.size(); ++index) {
        if (m_timerData[index] == data) {
            m_timers[index]->stop();
            m_timerCallbacks.remove(index);
            m_timers.remove(index);
            m_timerData.remove(index);
            return;
        }
    }
}

void ThreadDebugger::onTimer(Timer<ThreadDebugger>* timer)
{
    for (size_t index = 0; index < m_timers.size(); ++index) {
        if (m_timers[index] == timer) {
            m_timerCallbacks[index](m_timerData[index]);
            return;
        }
    }
}

} // namespace blink
