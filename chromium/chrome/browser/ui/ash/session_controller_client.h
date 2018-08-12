// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_SESSION_CONTROLLER_CLIENT_H_
#define CHROME_BROWSER_UI_ASH_SESSION_CONTROLLER_CLIENT_H_

#include <vector>

#include "ash/public/interfaces/session_controller.mojom.h"
#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/session_manager/core/session_manager_observer.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "mojo/public/cpp/bindings/binding.h"

class Profile;
class PrefChangeRegistrar;

namespace ash {
enum class AddUserSessionPolicy;
}

namespace user_manager {
class User;
}

// Updates session state etc to ash via SessionController interface and handles
// session related calls from ash.
// TODO(xiyuan): Update when UserSessionStateObserver is gone.
class SessionControllerClient
    : public ash::mojom::SessionControllerClient,
      public user_manager::UserManager::UserSessionStateObserver,
      public user_manager::UserManager::Observer,
      public session_manager::SessionManagerObserver,
      public content::NotificationObserver {
 public:
  SessionControllerClient();
  ~SessionControllerClient() override;

  void Init();

  static SessionControllerClient* Get();

  // Calls ash SessionController to run unlock animation.
  // |animation_finished_callback| will be invoked when the animation finishes.
  void RunUnlockAnimation(base::Closure animation_finished_callback);

  // ash::mojom::SessionControllerClient:
  void RequestLockScreen() override;
  void SwitchActiveUser(const AccountId& account_id) override;
  void CycleActiveUser(ash::CycleUserDirection direction) override;

  // user_manager::UserManager::UserSessionStateObserver:
  void ActiveUserChanged(const user_manager::User* active_user) override;
  void UserAddedToSession(const user_manager::User* added_user) override;
  void UserChangedChildStatus(user_manager::User* user) override;

  // user_manager::UserManager::Observer
  void OnUserImageChanged(const user_manager::User& user) override;

  // session_manager::SessionManagerObserver:
  void OnSessionStateChanged() override;

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // TODO(xiyuan): Remove after SessionStateDelegateChromeOS is gone.
  static bool CanLockScreen();
  static bool ShouldLockScreenAutomatically();
  static ash::AddUserSessionPolicy GetAddUserSessionPolicy();
  static void DoLockScreen();
  static void DoSwitchActiveUser(const AccountId& account_id);
  static void DoCycleActiveUser(ash::CycleUserDirection direction);

  // Flushes the mojo pipe to ash.
  static void FlushForTesting();

 private:
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, SendUserSession);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, SupervisedUser);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, UserPrefsChange);

  // Called when the login profile is ready.
  void OnLoginUserProfilePrepared(Profile* profile);

  // Connects to the |session_controller_| interface.
  void ConnectToSessionController();

  // Sends session info to ash.
  void SendSessionInfoIfChanged();

  // Sends the user session info.
  void SendUserSession(const user_manager::User& user);

  // Sends the order of user sessions to ash.
  void SendUserSessionOrder();

  // Binds to the client interface.
  mojo::Binding<ash::mojom::SessionControllerClient> binding_;

  // SessionController interface in ash.
  ash::mojom::SessionControllerPtr session_controller_;

  // Whether the primary user session info is sent to ash.
  bool primary_user_session_sent_ = false;

  content::NotificationRegistrar registrar_;

  // Pref change observers to update session info when a relevant user pref
  // changes. There is one observer per user and they have no particular order,
  // i.e. they don't much the user session order.
  std::vector<std::unique_ptr<PrefChangeRegistrar>> pref_change_registrars_;

  // Used to suppress duplicate IPCs to ash.
  ash::mojom::SessionInfoPtr last_sent_session_info_;

  base::WeakPtrFactory<SessionControllerClient> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SessionControllerClient);
};

#endif  // CHROME_BROWSER_UI_ASH_SESSION_CONTROLLER_CLIENT_H_
