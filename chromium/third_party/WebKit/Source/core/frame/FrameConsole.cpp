/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

#include "core/frame/FrameConsole.h"

#include "bindings/core/v8/ScriptCallStack.h"
#include "core/frame/FrameHost.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/ConsoleMessageStorage.h"
#include "core/inspector/InspectorConsoleInstrumentation.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "platform/network/ResourceError.h"
#include "platform/network/ResourceResponse.h"
#include "wtf/text/StringBuilder.h"

namespace blink {

static const HashSet<int>& allClientReportingMessageTypes()
{
    DEFINE_STATIC_LOCAL(HashSet<int>, types, ());
    if (types.isEmpty()) {
        types.add(LogMessageType);
        types.add(DirMessageType);
        types.add(DirXMLMessageType);
        types.add(TableMessageType);
        types.add(TraceMessageType);
        types.add(ClearMessageType);
        types.add(AssertMessageType);
    }
    return types;
}

namespace {

int muteCount = 0;

}

FrameConsole::FrameConsole(LocalFrame& frame)
    : m_frame(&frame)
{
}

void FrameConsole::addMessage(ConsoleMessage* consoleMessage)
{
    if (muteCount && consoleMessage->source() != ConsoleAPIMessageSource)
        return;

    // FIXME: This should not need to reach for the main-frame.
    // Inspector code should just take the current frame and know how to walk itself.
    ExecutionContext* context = frame().document();
    if (!context)
        return;
    if (!messageStorage())
        return;

    String messageURL;
    unsigned lineNumber = 0;
    if (consoleMessage->callStack() && !consoleMessage->callStack()->isEmpty()) {
        lineNumber = consoleMessage->callStack()->topLineNumber();
        messageURL = consoleMessage->callStack()->topSourceURL();
    } else {
        lineNumber = consoleMessage->lineNumber();
        messageURL = consoleMessage->url();
    }

    messageStorage()->reportMessage(m_frame->document(), consoleMessage);

    if (consoleMessage->source() == NetworkMessageSource)
        return;

    RefPtr<ScriptCallStack> reportedCallStack;
    if (consoleMessage->source() != ConsoleAPIMessageSource) {
        if (consoleMessage->callStack() && frame().chromeClient().shouldReportDetailedMessageForSource(frame(), messageURL))
            reportedCallStack = consoleMessage->callStack();
    } else {
        if (!frame().host() || (consoleMessage->scriptArguments() && !consoleMessage->scriptArguments()->argumentCount()))
            return;

        if (!allClientReportingMessageTypes().contains(consoleMessage->type()))
            return;

        if (frame().chromeClient().shouldReportDetailedMessageForSource(frame(), messageURL))
            reportedCallStack = ScriptCallStack::capture();
    }

    String stackTrace;
    if (reportedCallStack)
        stackTrace = reportedCallStack->toString();
    frame().chromeClient().addMessageToConsole(m_frame, consoleMessage->source(), consoleMessage->level(), consoleMessage->message(), lineNumber, messageURL, stackTrace);
}

void FrameConsole::reportResourceResponseReceived(DocumentLoader* loader, unsigned long requestIdentifier, const ResourceResponse& response)
{
    if (!loader)
        return;
    if (response.httpStatusCode() < 400)
        return;
    if (response.wasFallbackRequiredByServiceWorker())
        return;
    String message = "Failed to load resource: the server responded with a status of " + String::number(response.httpStatusCode()) + " (" + response.httpStatusText() + ')';
    ConsoleMessage* consoleMessage = ConsoleMessage::create(NetworkMessageSource, ErrorMessageLevel, message, response.url().getString(), 0);
    consoleMessage->setRequestIdentifier(requestIdentifier);
    addMessage(consoleMessage);
}

void FrameConsole::mute()
{
    muteCount++;
}

void FrameConsole::unmute()
{
    ASSERT(muteCount > 0);
    muteCount--;
}

ConsoleMessageStorage* FrameConsole::messageStorage()
{
    if (!m_frame->host())
        return nullptr;
    return &m_frame->host()->consoleMessageStorage();
}

void FrameConsole::clearMessages()
{
    ConsoleMessageStorage* storage = messageStorage();
    if (storage)
        storage->clear(m_frame->document());
}

void FrameConsole::adoptWorkerMessagesAfterTermination(WorkerInspectorProxy* proxy)
{
    ConsoleMessageStorage* storage = messageStorage();
    if (storage)
        storage->adoptWorkerMessagesAfterTermination(proxy);
}

void FrameConsole::didFailLoading(unsigned long requestIdentifier, const ResourceError& error)
{
    if (error.isCancellation()) // Report failures only.
        return;
    ConsoleMessageStorage* storage = messageStorage();
    if (!storage)
        return;
    StringBuilder message;
    message.appendLiteral("Failed to load resource");
    if (!error.localizedDescription().isEmpty()) {
        message.appendLiteral(": ");
        message.append(error.localizedDescription());
    }
    ConsoleMessage* consoleMessage = ConsoleMessage::create(NetworkMessageSource, ErrorMessageLevel, message.toString(), error.failingURL(), 0);
    consoleMessage->setRequestIdentifier(requestIdentifier);
    storage->reportMessage(m_frame->document(), consoleMessage);
}

DEFINE_TRACE(FrameConsole)
{
    visitor->trace(m_frame);
}

} // namespace blink
