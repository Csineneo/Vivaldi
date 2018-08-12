// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/permissions/PermissionStatus.h"

#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/dom/Document.h"
#include "core/events/Event.h"
#include "modules/EventTargetModulesNames.h"
#include "modules/permissions/PermissionUtils.h"
#include "public/platform/Platform.h"
#include "wtf/Functional.h"

namespace blink {

// static
PermissionStatus* PermissionStatus::take(ScriptPromiseResolver* resolver,
                                         MojoPermissionStatus status,
                                         MojoPermissionDescriptor descriptor) {
  return PermissionStatus::createAndListen(resolver->getExecutionContext(),
                                           status, std::move(descriptor));
}

PermissionStatus* PermissionStatus::createAndListen(
    ExecutionContext* executionContext,
    MojoPermissionStatus status,
    MojoPermissionDescriptor descriptor) {
  PermissionStatus* permissionStatus =
      new PermissionStatus(executionContext, status, std::move(descriptor));
  permissionStatus->suspendIfNeeded();
  permissionStatus->startListening();
  return permissionStatus;
}

PermissionStatus::PermissionStatus(ExecutionContext* executionContext,
                                   MojoPermissionStatus status,
                                   MojoPermissionDescriptor descriptor)
    : ActiveScriptWrappable(this),
      ActiveDOMObject(executionContext),
      m_status(status),
      m_descriptor(std::move(descriptor)) {}

PermissionStatus::~PermissionStatus() {
  stopListening();
}

const AtomicString& PermissionStatus::interfaceName() const {
  return EventTargetNames::PermissionStatus;
}

ExecutionContext* PermissionStatus::getExecutionContext() const {
  return ActiveDOMObject::getExecutionContext();
}

void PermissionStatus::permissionChanged(MojoPermissionStatus status) {
  if (m_status == status)
    return;

  m_status = status;
  dispatchEvent(Event::create(EventTypeNames::change));

  m_service->GetNextPermissionChange(
      m_descriptor->Clone(), getExecutionContext()->getSecurityOrigin(),
      m_status,
      convertToBaseCallback(WTF::bind(&PermissionStatus::permissionChanged,
                                      wrapWeakPersistent(this))));
}

bool PermissionStatus::hasPendingActivity() const {
  return m_service;
}

void PermissionStatus::resume() {
  startListening();
}

void PermissionStatus::suspend() {
  stopListening();
}

void PermissionStatus::contextDestroyed() {
  stopListening();
}

void PermissionStatus::startListening() {
  DCHECK(!m_service);
  connectToPermissionService(getExecutionContext(), mojo::GetProxy(&m_service));
  m_service->GetNextPermissionChange(
      m_descriptor->Clone(), getExecutionContext()->getSecurityOrigin(),
      m_status,
      convertToBaseCallback(WTF::bind(&PermissionStatus::permissionChanged,
                                      wrapWeakPersistent(this))));
}

void PermissionStatus::stopListening() {
  m_service.reset();
}

String PermissionStatus::state() const {
  switch (m_status) {
    case MojoPermissionStatus::GRANTED:
      return "granted";
    case MojoPermissionStatus::DENIED:
      return "denied";
    case MojoPermissionStatus::ASK:
      return "prompt";
  }

  ASSERT_NOT_REACHED();
  return "denied";
}

DEFINE_TRACE(PermissionStatus) {
  EventTargetWithInlineData::trace(visitor);
  ActiveDOMObject::trace(visitor);
}

}  // namespace blink
