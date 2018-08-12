/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Matt Lilek <webkit@mattlilek.com>
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/v8_inspector/InjectedScriptHost.h"

#include "platform/inspector_protocol/String16.h"
#include "platform/inspector_protocol/Values.h"
#include "platform/v8_inspector/V8DebuggerAgentImpl.h"
#include "platform/v8_inspector/V8InspectorSessionImpl.h"
#include "platform/v8_inspector/V8RuntimeAgentImpl.h"
#include "platform/v8_inspector/public/V8Debugger.h"

namespace blink {

PassOwnPtr<InjectedScriptHost> InjectedScriptHost::create(V8DebuggerImpl* debugger, V8InspectorSessionImpl* session)
{
    return adoptPtr(new InjectedScriptHost(debugger, session));
}

InjectedScriptHost::InjectedScriptHost(V8DebuggerImpl* debugger, V8InspectorSessionImpl* session)
    : m_debugger(debugger)
    , m_session(session)
{
}

InjectedScriptHost::~InjectedScriptHost()
{
}

void InjectedScriptHost::inspectImpl(PassOwnPtr<protocol::Value> object, PassOwnPtr<protocol::Value> hints)
{
    protocol::ErrorSupport errors;
    OwnPtr<protocol::Runtime::RemoteObject> remoteObject = protocol::Runtime::RemoteObject::parse(object.get(), &errors);
    m_session->runtimeAgentImpl()->inspect(remoteObject.release(), protocol::DictionaryValue::cast(hints));
}

void InjectedScriptHost::clearConsoleMessages()
{
    if (m_session->clearConsoleCallback())
        (*m_session->clearConsoleCallback())();
}

void InjectedScriptHost::addInspectedObject(PassOwnPtr<V8RuntimeAgent::Inspectable> object)
{
    m_inspectedObjects.prepend(object);
    while (m_inspectedObjects.size() > 5)
        m_inspectedObjects.removeLast();
}

void InjectedScriptHost::clearInspectedObjects()
{
    m_inspectedObjects.clear();
}

V8RuntimeAgent::Inspectable* InjectedScriptHost::inspectedObject(unsigned num)
{
    if (num >= m_inspectedObjects.size())
        return nullptr;
    return m_inspectedObjects[num].get();
}

void InjectedScriptHost::debugFunction(const String16& scriptId, int lineNumber, int columnNumber)
{
    m_session->debuggerAgentImpl()->setBreakpointAt(scriptId, lineNumber, columnNumber, V8DebuggerAgentImpl::DebugCommandBreakpointSource);
}

void InjectedScriptHost::undebugFunction(const String16& scriptId, int lineNumber, int columnNumber)
{
    m_session->debuggerAgentImpl()->removeBreakpointAt(scriptId, lineNumber, columnNumber, V8DebuggerAgentImpl::DebugCommandBreakpointSource);
}

void InjectedScriptHost::monitorFunction(const String16& scriptId, int lineNumber, int columnNumber, const String16& functionName)
{
    String16Builder builder;
    builder.append("console.log(\"function ");
    if (functionName.isEmpty())
        builder.append("(anonymous function)");
    else
        builder.append(functionName);
    builder.append(" called\" + (arguments.length > 0 ? \" with arguments: \" + Array.prototype.join.call(arguments, \", \") : \"\")) && false");
    m_session->debuggerAgentImpl()->setBreakpointAt(scriptId, lineNumber, columnNumber, V8DebuggerAgentImpl::MonitorCommandBreakpointSource, builder.toString());
}

void InjectedScriptHost::unmonitorFunction(const String16& scriptId, int lineNumber, int columnNumber)
{
    m_session->debuggerAgentImpl()->removeBreakpointAt(scriptId, lineNumber, columnNumber, V8DebuggerAgentImpl::MonitorCommandBreakpointSource);
}

} // namespace blink
