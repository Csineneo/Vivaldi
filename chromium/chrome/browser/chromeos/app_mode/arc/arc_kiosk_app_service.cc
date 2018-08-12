// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrome/browser/chromeos/app_mode/arc/arc_kiosk_app_service.h>

#include "chrome/browser/chromeos/app_mode/arc/arc_kiosk_app_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace chromeos {

// static
ArcKioskAppService* ArcKioskAppService::Create(Profile* profile,
                                               ArcAppListPrefs* prefs) {
  return new ArcKioskAppService(profile, prefs);
}

// static
ArcKioskAppService* ArcKioskAppService::Get(content::BrowserContext* context) {
  return ArcKioskAppServiceFactory::GetForBrowserContext(context);
}

void ArcKioskAppService::OnAppRegistered(
    const std::string& app_id,
    const ArcAppListPrefs::AppInfo& app_info) {
  PreconditionsChanged();
}

void ArcKioskAppService::OnAppReadyChanged(const std::string& id, bool ready) {
  PreconditionsChanged();
}

void ArcKioskAppService::OnPackageListInitialRefreshed() {
  // The app could already be registered.
  PreconditionsChanged();
}

void ArcKioskAppService::OnArcKioskAppsChanged() {
  PreconditionsChanged();
}

void ArcKioskAppService::OnTaskCreated(int32_t task_id,
                                       const std::string& package_name,
                                       const std::string& activity,
                                       const std::string& intent) {
  // Store task id of the app to stop it later when needed.
  if (app_info_ && package_name == app_info_->package_name &&
      activity == app_info_->activity) {
    task_id_ = task_id;
  }
}

void ArcKioskAppService::OnTaskDestroyed(int32_t task_id) {
  if (task_id == task_id_) {
    app_launcher_.reset();
    task_id_ = -1;
    // Trying to restart app if it was somehow closed or crashed
    // as kiosk app should always be running during the session.
    PreconditionsChanged();
  }
}

ArcKioskAppService::ArcKioskAppService(Profile* profile, ArcAppListPrefs* prefs)
    : profile_(profile), prefs_(prefs) {
  if (prefs_)
    prefs_->AddObserver(this);
  app_manager_ = ArcKioskAppManager::Get();
  if (app_manager_) {
    app_manager_->AddObserver(this);
  }
  pref_change_registrar_.reset(new PrefChangeRegistrar());
  pref_change_registrar_->Init(profile_->GetPrefs());
  // Try to start/stop kiosk app on policy compliance state change.
  pref_change_registrar_->Add(
      prefs::kArcPolicyCompliant,
      base::Bind(&ArcKioskAppService::PreconditionsChanged,
                 base::Unretained(this)));
  PreconditionsChanged();
}

ArcKioskAppService::~ArcKioskAppService() {
  if (prefs_)
    prefs_->RemoveObserver(this);
  if (app_manager_)
    app_manager_->RemoveObserver(this);
}

void ArcKioskAppService::PreconditionsChanged() {
  app_id_ = GetAppId();
  if (app_id_.empty())
    return;
  app_info_ = prefs_->GetApp(app_id_);
  if (app_info_ && app_info_->ready &&
      profile_->GetPrefs()->GetBoolean(prefs::kArcPolicyCompliant)) {
    if (!app_launcher_)
      app_launcher_.reset(new ArcKioskAppLauncher(profile_, prefs_, app_id_));
  } else if (task_id_ != -1) {
    arc::CloseTask(task_id_);
  }
}

std::string ArcKioskAppService::GetAppId() {
  AccountId account_id = multi_user_util::GetAccountIdFromProfile(profile_);
  const ArcKioskAppManager::ArcKioskApp* app =
      app_manager_->GetAppByAccountId(account_id);
  if (!app)
    return std::string();
  std::unordered_set<std::string> app_ids =
      prefs_->GetAppsForPackage(app->app_info().package_name());
  if (app_ids.empty())
    return std::string();
  // TODO(poromov@): Choose appropriate app id to launch. See
  // http://crbug.com/665904
  return std::string(*app_ids.begin());
}

}  // namespace chromeos
