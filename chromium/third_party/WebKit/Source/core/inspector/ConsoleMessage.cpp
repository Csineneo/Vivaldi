// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/inspector/ConsoleMessage.h"

#include "bindings/core/v8/ScriptCallStack.h"
#include "bindings/core/v8/ScriptValue.h"
#include "core/inspector/ScriptArguments.h"
#include "core/workers/WorkerInspectorProxy.h"
#include "wtf/CurrentTime.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

unsigned nextMessageId()
{
    struct MessageId {
        MessageId() : value(0) { }
        unsigned value;
    };

    DEFINE_THREAD_SAFE_STATIC_LOCAL(WTF::ThreadSpecific<MessageId>, messageId, new WTF::ThreadSpecific<MessageId>);
    return ++messageId->value;
}

ConsoleMessage::ConsoleMessage(MessageSource source,
    MessageLevel level,
    const String& message,
    const String& url,
    unsigned lineNumber,
    unsigned columnNumber)
    : m_source(source)
    , m_level(level)
    , m_type(LogMessageType)
    , m_message(message)
    , m_scriptId(0)
    , m_url(url)
    , m_lineNumber(lineNumber)
    , m_columnNumber(columnNumber)
    , m_requestIdentifier(0)
    , m_timestamp(WTF::currentTime())
    , m_workerProxy(nullptr)
    , m_messageId(0)
    , m_relatedMessageId(0)
{
}

ConsoleMessage::~ConsoleMessage()
{
}

MessageType ConsoleMessage::type() const
{
    return m_type;
}

void ConsoleMessage::setType(MessageType type)
{
    m_type = type;
}

int ConsoleMessage::scriptId() const
{
    return m_scriptId;
}

void ConsoleMessage::setScriptId(int scriptId)
{
    m_scriptId = scriptId;
}

const String& ConsoleMessage::url() const
{
    return m_url;
}

void ConsoleMessage::setURL(const String& url)
{
    m_url = url;
}

unsigned ConsoleMessage::lineNumber() const
{
    return m_lineNumber;
}

void ConsoleMessage::setLineNumber(unsigned lineNumber)
{
    m_lineNumber = lineNumber;
}

unsigned ConsoleMessage::columnNumber() const
{
    return m_columnNumber;
}

void ConsoleMessage::setColumnNumber(unsigned columnNumber)
{
    m_columnNumber = columnNumber;
}

PassRefPtr<ScriptCallStack> ConsoleMessage::callStack() const
{
    return m_callStack;
}

void ConsoleMessage::setCallStack(PassRefPtr<ScriptCallStack> callStack)
{
    m_callStack = callStack;
    if (m_callStack && !m_callStack->isEmpty() && !m_scriptId) {
        m_url = m_callStack->topSourceURL();
        m_lineNumber = m_callStack->topLineNumber();
        m_columnNumber = m_callStack->topColumnNumber();
    }
}

ScriptState* ConsoleMessage::getScriptState() const
{
    if (m_scriptState)
        return m_scriptState->get();
    return nullptr;
}

void ConsoleMessage::setScriptState(ScriptState* scriptState)
{
    if (m_scriptState)
        m_scriptState->clear();

    if (scriptState)
        m_scriptState = adoptPtr(new ScriptStateProtectingContext(scriptState));
    else
        m_scriptState.clear();
}

ScriptArguments* ConsoleMessage::scriptArguments() const
{
    return m_scriptArguments;
}

void ConsoleMessage::setScriptArguments(ScriptArguments* scriptArguments)
{
    m_scriptArguments = scriptArguments;
}

unsigned long ConsoleMessage::requestIdentifier() const
{
    return m_requestIdentifier;
}

void ConsoleMessage::setRequestIdentifier(unsigned long requestIdentifier)
{
    m_requestIdentifier = requestIdentifier;
}

double ConsoleMessage::timestamp() const
{
    return m_timestamp;
}

void ConsoleMessage::setTimestamp(double timestamp)
{
    m_timestamp = timestamp;
}

unsigned ConsoleMessage::assignMessageId()
{
    if (!m_messageId)
        m_messageId = nextMessageId();
    return m_messageId;
}

MessageSource ConsoleMessage::source() const
{
    return m_source;
}

MessageLevel ConsoleMessage::level() const
{
    return m_level;
}

const String& ConsoleMessage::message() const
{
    return m_message;
}

void ConsoleMessage::frameWindowDiscarded(LocalDOMWindow* window)
{
    if (getScriptState() && getScriptState()->domWindow() == window)
        setScriptState(nullptr);

    if (!m_scriptArguments)
        return;
    if (m_scriptArguments->getScriptState()->domWindow() != window)
        return;
    if (!m_message)
        m_message = "<message collected>";
    m_scriptArguments.clear();
}

unsigned ConsoleMessage::argumentCount()
{
    if (m_scriptArguments)
        return m_scriptArguments->argumentCount();
    return 0;
}

void ConsoleMessage::collectCallStack()
{
    if (m_type == EndGroupMessageType)
        return;

    if (!m_callStack)
        setCallStack(ScriptCallStack::captureForConsole());
}

DEFINE_TRACE(ConsoleMessage)
{
    visitor->trace(m_scriptArguments);
    visitor->trace(m_workerProxy);
}

} // namespace blink
