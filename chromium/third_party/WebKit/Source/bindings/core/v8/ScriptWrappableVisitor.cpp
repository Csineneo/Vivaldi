// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/ScriptWrappableVisitor.h"

#include "bindings/core/v8/ActiveScriptWrappable.h"
#include "bindings/core/v8/DOMWrapperWorld.h"
#include "bindings/core/v8/ScriptWrappableVisitorVerifier.h"
#include "bindings/core/v8/V8AbstractEventListener.h"
#include "bindings/core/v8/WrapperTypeInfo.h"
#include "core/dom/DocumentStyleSheetCollection.h"
#include "core/dom/ElementRareData.h"
#include "core/dom/NodeListsNodeData.h"
#include "core/dom/NodeRareData.h"
#include "core/dom/StyleEngine.h"
#include "core/dom/shadow/ElementShadow.h"
#include "core/html/imports/HTMLImportsController.h"
#include "platform/heap/HeapPage.h"
#include "public/platform/Platform.h"
#include "public/platform/WebScheduler.h"
#include "wtf/AutoReset.h"

namespace blink {

ScriptWrappableVisitor::~ScriptWrappableVisitor() {}

void ScriptWrappableVisitor::TracePrologue(
    v8::EmbedderReachableReferenceReporter* reporter) {
  performCleanup();

  DCHECK(!m_tracingInProgress);
  DCHECK(!m_shouldCleanup);
  DCHECK(m_headersToUnmark.isEmpty());
  DCHECK(m_markingDeque.isEmpty());
  DCHECK(m_verifierDeque.isEmpty());
  DCHECK(!m_reporter);
  DCHECK(reporter);
  m_tracingInProgress = true;
  m_reporter = reporter;
}

void ScriptWrappableVisitor::EnterFinalPause() {
  ActiveScriptWrappable::traceActiveScriptWrappables(m_isolate, this);
}

void ScriptWrappableVisitor::TraceEpilogue() {
  DCHECK(m_markingDeque.isEmpty());
#if DCHECK_IS_ON()
  ScriptWrappableVisitorVerifier verifier;
  for (auto& markingData : m_verifierDeque) {
    markingData.traceWrappers(&verifier);
  }
#endif

  m_shouldCleanup = true;
  scheduleIdleLazyCleanup();
}

void ScriptWrappableVisitor::AbortTracing() {
  m_shouldCleanup = true;
  performCleanup();
}

size_t ScriptWrappableVisitor::NumberOfWrappersToTrace() {
  return m_markingDeque.size();
}

void ScriptWrappableVisitor::performCleanup() {
  if (!m_shouldCleanup)
    return;

  for (auto header : m_headersToUnmark) {
    // Dead objects residing in the marking deque may become invalid due to
    // minor garbage collections and are therefore set to nullptr. We have
    // to skip over such objects.
    if (header)
      header->unmarkWrapperHeader();
  }

  m_headersToUnmark.clear();
  m_markingDeque.clear();
  m_verifierDeque.clear();
  m_shouldCleanup = false;
  m_tracingInProgress = false;
  m_reporter = nullptr;
}

void ScriptWrappableVisitor::scheduleIdleLazyCleanup() {
  // Some threads (e.g. PPAPI thread) don't have a scheduler.
  if (!Platform::current()->currentThread()->scheduler())
    return;

  if (m_idleCleanupTaskScheduled)
    return;

  Platform::current()->currentThread()->scheduler()->postIdleTask(
      BLINK_FROM_HERE, WTF::bind(&ScriptWrappableVisitor::performLazyCleanup,
                                 WTF::unretained(this)));
  m_idleCleanupTaskScheduled = true;
}

void ScriptWrappableVisitor::performLazyCleanup(double deadlineSeconds) {
  m_idleCleanupTaskScheduled = false;

  if (!m_shouldCleanup)
    return;

  TRACE_EVENT1("blink_gc,devtools.timeline",
               "ScriptWrappableVisitor::performLazyCleanup",
               "idleDeltaInSeconds",
               deadlineSeconds - monotonicallyIncreasingTime());

  const int kDeadlineCheckInterval = 2500;
  int processedWrapperCount = 0;
  for (auto it = m_headersToUnmark.rbegin(); it != m_headersToUnmark.rend();) {
    auto header = *it;
    // Dead objects residing in the marking deque may become invalid due to
    // minor garbage collections and are therefore set to nullptr. We have
    // to skip over such objects.
    if (header)
      header->unmarkWrapperHeader();

    ++it;
    m_headersToUnmark.removeLast();

    processedWrapperCount++;
    if (processedWrapperCount % kDeadlineCheckInterval == 0) {
      if (deadlineSeconds <= monotonicallyIncreasingTime()) {
        scheduleIdleLazyCleanup();
        return;
      }
    }
  }

  // Unmarked all headers.
  CHECK(m_headersToUnmark.isEmpty());
  m_markingDeque.clear();
  m_verifierDeque.clear();
  m_shouldCleanup = false;
  m_tracingInProgress = false;
}

void ScriptWrappableVisitor::RegisterV8Reference(
    const std::pair<void*, void*>& internalFields) {
  if (!m_tracingInProgress) {
    return;
  }

  WrapperTypeInfo* wrapperTypeInfo =
      reinterpret_cast<WrapperTypeInfo*>(internalFields.first);
  if (wrapperTypeInfo->ginEmbedder != gin::GinEmbedder::kEmbedderBlink) {
    return;
  }
  DCHECK(wrapperTypeInfo->wrapperClassId == WrapperTypeInfo::NodeClassId ||
         wrapperTypeInfo->wrapperClassId == WrapperTypeInfo::ObjectClassId);

  ScriptWrappable* scriptWrappable =
      reinterpret_cast<ScriptWrappable*>(internalFields.second);

  wrapperTypeInfo->traceWrappers(this, scriptWrappable);
}

void ScriptWrappableVisitor::RegisterV8References(
    const std::vector<std::pair<void*, void*>>&
        internalFieldsOfPotentialWrappers) {
  // TODO(hlopko): Visit the vector in the V8 instead of passing it over if
  // there is no performance impact
  for (auto& pair : internalFieldsOfPotentialWrappers) {
    RegisterV8Reference(pair);
  }
}

bool ScriptWrappableVisitor::AdvanceTracing(
    double deadlineInMs,
    v8::EmbedderHeapTracer::AdvanceTracingActions actions) {
  DCHECK(m_tracingInProgress);
  WTF::AutoReset<bool>(&m_advancingTracing, true);
  while (actions.force_completion ==
             v8::EmbedderHeapTracer::ForceCompletionAction::FORCE_COMPLETION ||
         WTF::monotonicallyIncreasingTimeMS() < deadlineInMs) {
    if (m_markingDeque.isEmpty()) {
      return false;
    }

    m_markingDeque.takeFirst().traceWrappers(this);
  }
  return true;
}

bool ScriptWrappableVisitor::markWrapperHeader(HeapObjectHeader* header) const {
  if (header->isWrapperHeaderMarked())
    return false;

  header->markWrapperHeader();
  m_headersToUnmark.append(header);
  return true;
}

void ScriptWrappableVisitor::markWrappersInAllWorlds(
    const ScriptWrappable* scriptWrappable) const {
  DCHECK(m_reporter);
  DOMWrapperWorld::markWrappersInAllWorlds(
      const_cast<ScriptWrappable*>(scriptWrappable), this, m_reporter);
}

void ScriptWrappableVisitor::traceWrappers(
    const ScopedPersistent<v8::Value>* scopedPersistent) const {
  markWrapper(
      &(const_cast<ScopedPersistent<v8::Value>*>(scopedPersistent)->get()));
}

void ScriptWrappableVisitor::traceWrappers(
    const ScopedPersistent<v8::Object>* scopedPersistent) const {
  markWrapper(
      &(const_cast<ScopedPersistent<v8::Object>*>(scopedPersistent)->get()));
}

void ScriptWrappableVisitor::markWrapper(
    const v8::PersistentBase<v8::Value>* handle) const {
  DCHECK(m_reporter);
  handle->RegisterExternalReference(m_reporter);
}

void ScriptWrappableVisitor::markWrapper(
    const v8::PersistentBase<v8::Object>* handle) const {
  DCHECK(m_reporter);
  handle->RegisterExternalReference(m_reporter);
}

void ScriptWrappableVisitor::dispatchTraceWrappers(
    const ScriptWrappable* wrappable) const {
  wrappable->traceWrappers(this);
}

#define DEFINE_DISPATCH_TRACE_WRAPPERS(className)     \
  void ScriptWrappableVisitor::dispatchTraceWrappers( \
      const className* traceable) const {             \
    traceable->traceWrappers(this);                   \
  }

WRAPPER_VISITOR_SPECIAL_CLASSES(DEFINE_DISPATCH_TRACE_WRAPPERS);

#undef DEFINE_DISPATCH_TRACE_WRAPPERS

void ScriptWrappableVisitor::invalidateDeadObjectsInMarkingDeque() {
  for (auto it = m_markingDeque.begin(); it != m_markingDeque.end(); ++it) {
    auto& markingData = *it;
    if (markingData.shouldBeInvalidated()) {
      markingData.invalidate();
    }
  }
  for (auto it = m_verifierDeque.begin(); it != m_verifierDeque.end(); ++it) {
    auto& markingData = *it;
    if (markingData.shouldBeInvalidated()) {
      markingData.invalidate();
    }
  }
  for (auto it = m_headersToUnmark.begin(); it != m_headersToUnmark.end();
       ++it) {
    auto header = *it;
    if (header && !header->isMarked()) {
      *it = nullptr;
    }
  }
}

void ScriptWrappableVisitor::invalidateDeadObjectsInMarkingDeque(
    v8::Isolate* isolate) {
  ScriptWrappableVisitor* scriptWrappableVisitor =
      V8PerIsolateData::from(isolate)->scriptWrappableVisitor();
  if (scriptWrappableVisitor)
    scriptWrappableVisitor->invalidateDeadObjectsInMarkingDeque();
}

void ScriptWrappableVisitor::performCleanup(v8::Isolate* isolate) {
  ScriptWrappableVisitor* scriptWrappableVisitor =
      V8PerIsolateData::from(isolate)->scriptWrappableVisitor();
  if (scriptWrappableVisitor)
    scriptWrappableVisitor->performCleanup();
}

WrapperVisitor* ScriptWrappableVisitor::currentVisitor(v8::Isolate* isolate) {
  return V8PerIsolateData::from(isolate)->scriptWrappableVisitor();
}

}  // namespace blink
