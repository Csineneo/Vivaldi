// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/custom/CustomElementMicrotaskDispatcher.h"

#include "bindings/core/v8/Microtask.h"
#include "core/dom/custom/CustomElementCallbackQueue.h"
#include "core/dom/custom/CustomElementMicrotaskImportStep.h"
#include "core/dom/custom/CustomElementProcessingStack.h"
#include "core/dom/custom/CustomElementScheduler.h"

namespace blink {

static const CustomElementCallbackQueue::ElementQueueId kMicrotaskQueueId = 0;

CustomElementMicrotaskDispatcher::CustomElementMicrotaskDispatcher()
    : m_hasScheduledMicrotask(false)
    , m_phase(Quiescent)
{
}

CustomElementMicrotaskDispatcher& CustomElementMicrotaskDispatcher::instance()
{
    DEFINE_STATIC_LOCAL(CustomElementMicrotaskDispatcher, instance, (new CustomElementMicrotaskDispatcher));
    return instance;
}

void CustomElementMicrotaskDispatcher::enqueue(CustomElementCallbackQueue* queue)
{
    ensureMicrotaskScheduledForElementQueue();
    queue->setOwner(kMicrotaskQueueId);
    m_elements.append(queue);
}

void CustomElementMicrotaskDispatcher::ensureMicrotaskScheduledForElementQueue()
{
    DCHECK(m_phase == Quiescent || m_phase == Resolving);
    ensureMicrotaskScheduled();
}

void CustomElementMicrotaskDispatcher::ensureMicrotaskScheduled()
{
    if (!m_hasScheduledMicrotask) {
        Microtask::enqueueMicrotask(WTF::bind(&dispatch));
        m_hasScheduledMicrotask = true;
    }
}

void CustomElementMicrotaskDispatcher::dispatch()
{
    instance().doDispatch();
}

void CustomElementMicrotaskDispatcher::doDispatch()
{
    DCHECK(isMainThread());

    DCHECK(m_phase == Quiescent);
    DCHECK(m_hasScheduledMicrotask);
    m_hasScheduledMicrotask = false;

    // Finishing microtask work deletes all
    // CustomElementCallbackQueues. Being in a callback delivery scope
    // implies those queues could still be in use.
    ASSERT_WITH_SECURITY_IMPLICATION(!CustomElementProcessingStack::inCallbackDeliveryScope());

    m_phase = Resolving;

    m_phase = DispatchingCallbacks;
    for (const auto& element : m_elements) {
        // Created callback may enqueue an attached callback.
        CustomElementProcessingStack::CallbackDeliveryScope scope;
        element->processInElementQueue(kMicrotaskQueueId);
    }

    m_elements.clear();
    CustomElementScheduler::microtaskDispatcherDidFinish();
    m_phase = Quiescent;
}

DEFINE_TRACE(CustomElementMicrotaskDispatcher)
{
    visitor->trace(m_elements);
}

} // namespace blink
