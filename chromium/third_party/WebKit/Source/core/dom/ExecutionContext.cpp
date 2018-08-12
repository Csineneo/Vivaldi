/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 * Copyright (C) 2012 Google Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "core/dom/ExecutionContext.h"

#include "bindings/core/v8/ScriptCallStack.h"
#include "core/dom/ExecutionContextTask.h"
#include "core/events/ErrorEvent.h"
#include "core/events/EventTarget.h"
#include "core/fetch/MemoryCache.h"
#include "core/frame/UseCounter.h"
#include "core/html/PublicURLManager.h"
#include "core/inspector/InspectorInstrumentation.h"
#include "core/workers/WorkerGlobalScope.h"
#include "core/workers/WorkerThread.h"

namespace blink {

class ExecutionContext::PendingException {
    WTF_MAKE_NONCOPYABLE(PendingException);
public:
    PendingException(const String& errorMessage, int lineNumber, int columnNumber, int scriptId, const String& sourceURL, PassRefPtr<ScriptCallStack> callStack)
        : m_errorMessage(errorMessage)
        , m_lineNumber(lineNumber)
        , m_columnNumber(columnNumber)
        , m_scriptId(scriptId)
        , m_sourceURL(sourceURL)
        , m_callStack(callStack)
    {
    }

    String m_errorMessage;
    int m_lineNumber;
    int m_columnNumber;
    int m_scriptId;
    String m_sourceURL;
    RefPtr<ScriptCallStack> m_callStack;
};

ExecutionContext::ExecutionContext()
    : m_circularSequentialID(0)
    , m_inDispatchErrorEvent(false)
    , m_activeDOMObjectsAreSuspended(false)
    , m_activeDOMObjectsAreStopped(false)
    , m_windowInteractionTokens(0)
    , m_isRunSuspendableTasksScheduled(false)
    , m_referrerPolicy(ReferrerPolicyDefault)
{
}

ExecutionContext::~ExecutionContext()
{
}

void ExecutionContext::suspendActiveDOMObjects()
{
    DCHECK(!m_activeDOMObjectsAreSuspended);
    notifySuspendingActiveDOMObjects();
    m_activeDOMObjectsAreSuspended = true;
}

void ExecutionContext::resumeActiveDOMObjects()
{
    DCHECK(m_activeDOMObjectsAreSuspended);
    m_activeDOMObjectsAreSuspended = false;
    notifyResumingActiveDOMObjects();
}

void ExecutionContext::stopActiveDOMObjects()
{
    m_activeDOMObjectsAreStopped = true;
    notifyStoppingActiveDOMObjects();
}

void ExecutionContext::postSuspendableTask(PassOwnPtr<SuspendableTask> task)
{
    m_suspendedTasks.append(std::move(task));
    if (!m_activeDOMObjectsAreSuspended)
        postTask(BLINK_FROM_HERE, createSameThreadTask(&ExecutionContext::runSuspendableTasks, this));
}

void ExecutionContext::notifyContextDestroyed()
{
    Deque<OwnPtr<SuspendableTask>> suspendedTasks;
    suspendedTasks.swap(m_suspendedTasks);
    for (Deque<OwnPtr<SuspendableTask>>::iterator it = suspendedTasks.begin(); it != suspendedTasks.end(); ++it)
        (*it)->contextDestroyed();
    ContextLifecycleNotifier::notifyContextDestroyed();
}

void ExecutionContext::suspendScheduledTasks()
{
    suspendActiveDOMObjects();
    tasksWereSuspended();
}

void ExecutionContext::resumeScheduledTasks()
{
    resumeActiveDOMObjects();
    tasksWereResumed();
    // We need finish stack unwiding before running next task because it can suspend this context.
    if (m_isRunSuspendableTasksScheduled)
        return;
    m_isRunSuspendableTasksScheduled = true;
    postTask(BLINK_FROM_HERE, createSameThreadTask(&ExecutionContext::runSuspendableTasks, this));
}

void ExecutionContext::suspendActiveDOMObjectIfNeeded(ActiveDOMObject* object)
{
#if DCHECK_IS_ON()
    DCHECK(contains(object));
#endif
    // Ensure all ActiveDOMObjects are suspended also newly created ones.
    if (m_activeDOMObjectsAreSuspended)
        object->suspend();
}

bool ExecutionContext::shouldSanitizeScriptError(const String& sourceURL, AccessControlStatus corsStatus)
{
    if (corsStatus == OpaqueResource)
        return true;
    return !(getSecurityOrigin()->canRequestNoSuborigin(completeURL(sourceURL)) || corsStatus == SharableCrossOrigin);
}

void ExecutionContext::reportException(ErrorEvent* errorEvent, int scriptId, PassRefPtr<ScriptCallStack> callStack, AccessControlStatus corsStatus)
{
    if (m_inDispatchErrorEvent) {
        if (!m_pendingExceptions)
            m_pendingExceptions = adoptPtr(new Vector<OwnPtr<PendingException>>());
        m_pendingExceptions->append(adoptPtr(new PendingException(errorEvent->messageForConsole(), errorEvent->lineno(), errorEvent->colno(), scriptId, errorEvent->filename(), callStack)));
        return;
    }

    // First report the original exception and only then all the nested ones.
    if (!dispatchErrorEvent(errorEvent, corsStatus))
        logExceptionToConsole(errorEvent->messageForConsole(), scriptId, errorEvent->filename(), errorEvent->lineno(), errorEvent->colno(), callStack);

    if (!m_pendingExceptions)
        return;

    for (size_t i = 0; i < m_pendingExceptions->size(); i++) {
        PendingException* e = m_pendingExceptions->at(i).get();
        logExceptionToConsole(e->m_errorMessage, e->m_scriptId, e->m_sourceURL, e->m_lineNumber, e->m_columnNumber, e->m_callStack);
    }
    m_pendingExceptions.clear();
}

bool ExecutionContext::dispatchErrorEvent(ErrorEvent* errorEvent, AccessControlStatus corsStatus)
{
    EventTarget* target = errorEventTarget();
    if (!target)
        return false;

    if (shouldSanitizeScriptError(errorEvent->filename(), corsStatus))
        errorEvent = ErrorEvent::createSanitizedError(errorEvent->world());

    DCHECK(!m_inDispatchErrorEvent);
    m_inDispatchErrorEvent = true;
    target->dispatchEvent(errorEvent);
    m_inDispatchErrorEvent = false;
    return errorEvent->defaultPrevented();
}

void ExecutionContext::runSuspendableTasks()
{
    m_isRunSuspendableTasksScheduled = false;
    while (!m_activeDOMObjectsAreSuspended && m_suspendedTasks.size()) {
        OwnPtr<SuspendableTask> task = m_suspendedTasks.takeFirst();
        task->run();
    }
}

int ExecutionContext::circularSequentialID()
{
    ++m_circularSequentialID;
    if (m_circularSequentialID > ((1U << 31) - 1U))
        m_circularSequentialID = 1;

    return m_circularSequentialID;
}

PublicURLManager& ExecutionContext::publicURLManager()
{
    if (!m_publicURLManager)
        m_publicURLManager = PublicURLManager::create(this);
    return *m_publicURLManager;
}

SecurityOrigin* ExecutionContext::getSecurityOrigin()
{
    return securityContext().getSecurityOrigin();
}

ContentSecurityPolicy* ExecutionContext::contentSecurityPolicy()
{
    return securityContext().contentSecurityPolicy();
}

const KURL& ExecutionContext::url() const
{
    return virtualURL();
}

KURL ExecutionContext::completeURL(const String& url) const
{
    return virtualCompleteURL(url);
}

void ExecutionContext::allowWindowInteraction()
{
    ++m_windowInteractionTokens;
}

void ExecutionContext::consumeWindowInteraction()
{
    if (m_windowInteractionTokens == 0)
        return;
    --m_windowInteractionTokens;
}

bool ExecutionContext::isWindowInteractionAllowed() const
{
    return m_windowInteractionTokens > 0;
}

bool ExecutionContext::isSecureContext(const SecureContextCheck privilegeContextCheck) const
{
    String unusedErrorMessage;
    return isSecureContext(unusedErrorMessage, privilegeContextCheck);
}

String ExecutionContext::outgoingReferrer() const
{
    return url().strippedForUseAsReferrer();
}

void ExecutionContext::setReferrerPolicy(ReferrerPolicy referrerPolicy)
{
    // When a referrer policy has already been set, the latest value takes precedence.
    UseCounter::count(this, UseCounter::SetReferrerPolicy);
    if (m_referrerPolicy != ReferrerPolicyDefault)
        UseCounter::count(this, UseCounter::ResetReferrerPolicy);

    m_referrerPolicy = referrerPolicy;
}

void ExecutionContext::removeURLFromMemoryCache(const KURL& url)
{
    memoryCache()->removeURLFromCache(url);
}

DEFINE_TRACE(ExecutionContext)
{
    visitor->trace(m_publicURLManager);
    ContextLifecycleNotifier::trace(visitor);
    Supplementable<ExecutionContext>::trace(visitor);
}

} // namespace blink
