// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SESSION_SESSION_STATE_OBSERVER_H_
#define ASH_SESSION_SESSION_STATE_OBSERVER_H_

#include "ash/ash_export.h"
#include "base/macros.h"
#include "components/session_manager/session_manager_types.h"

class AccountId;

namespace ash {

enum class LoginStatus;

// TODO(xiyuan): Rename to On*Changed().
class ASH_EXPORT SessionStateObserver {
 public:
  // Called when active user has changed.
  virtual void ActiveUserChanged(const AccountId& account_id) {}

  // Called when another user gets added to the existing session.
  virtual void UserAddedToSession(const AccountId& account_id) {}

  // Called when a user session is updated, such as avatar change.
  virtual void UserSessionUpdated(const AccountId& account_id) {}

  // Called when the session state is changed.
  virtual void SessionStateChanged(session_manager::SessionState state) {}

  // Called when the login status is changed. |login_status| is the new status.
  virtual void LoginStatusChanged(LoginStatus login_status) {}

  // Called when the lock state is changed. |locked| is the current lock stated.
  virtual void LockStateChanged(bool locked) {}

 protected:
  virtual ~SessionStateObserver() {}
};

// A class to attach / detach an object as a session state observer.
class ASH_EXPORT ScopedSessionStateObserver {
 public:
  explicit ScopedSessionStateObserver(SessionStateObserver* observer);
  virtual ~ScopedSessionStateObserver();

 private:
  ash::SessionStateObserver* observer_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSessionStateObserver);
};

}  // namespace ash

#endif  // ASH_SESSION_SESSION_STATE_OBSERVER_H_
