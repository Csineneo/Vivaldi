// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/arc_auth_service.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/chromeos/arc/arc_auth_notification.h"
#include "chrome/browser/chromeos/arc/arc_optin_uma.h"
#include "chrome/browser/chromeos/arc/arc_support_host.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "chrome/browser/prefs/pref_service_syncable_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/app_list/arc/arc_app_launcher.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/chromeos_switches.h"
#include "components/arc/arc_bridge_service.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "components/syncable_prefs/pref_service_syncable.h"
#include "components/user_manager/user.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "google_apis/gaia/gaia_constants.h"
#include "ui/base/l10n/l10n_util.h"

namespace arc {

namespace {

// Weak pointer.  This class is owned by ArcServiceManager.
ArcAuthService* arc_auth_service = nullptr;

base::LazyInstance<base::ThreadChecker> thread_checker =
    LAZY_INSTANCE_INITIALIZER;

// Skip creating UI in unit tests
bool disable_ui_for_testing = false;

// The Android management check is disabled by default, it's used only for
// testing.
bool enable_check_android_management_for_testing = false;

const char kStateNotInitialized[] = "NOT_INITIALIZED";
const char kStateStopped[] = "STOPPED";
const char kStateFetchingCode[] = "FETCHING_CODE";
const char kStateActive[] = "ACTIVE";

bool IsAccountManaged(Profile* profile) {
  return policy::ProfilePolicyConnectorFactory::GetForBrowserContext(profile)
      ->IsManaged();
}

bool IsArcDisabledForEnterprise() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kEnterpriseDisableArc);
}

}  // namespace

ArcAuthService::ArcAuthService(ArcBridgeService* bridge_service)
    : ArcService(bridge_service), binding_(this), weak_ptr_factory_(this) {
  DCHECK(!arc_auth_service);
  DCHECK(thread_checker.Get().CalledOnValidThread());

  arc_auth_service = this;

  arc_bridge_service()->AddObserver(this);
}

ArcAuthService::~ArcAuthService() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(arc_auth_service == this);

  Shutdown();
  arc_bridge_service()->RemoveObserver(this);

  arc_auth_service = nullptr;
}

// static
ArcAuthService* ArcAuthService::Get() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  return arc_auth_service;
}

// static
void ArcAuthService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kArcEnabled, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(prefs::kArcSignedIn, false);
}

// static
void ArcAuthService::DisableUIForTesting() {
  disable_ui_for_testing = true;
}

// static
bool ArcAuthService::IsOptInVerificationDisabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kDisableArcOptInVerification);
}

// static
void ArcAuthService::EnableCheckAndroidManagementForTesting() {
  enable_check_android_management_for_testing = true;
}

// static
bool ArcAuthService::IsAllowedForProfile(const Profile* profile) {
  if (!arc::ArcBridgeService::GetEnabled(
          base::CommandLine::ForCurrentProcess())) {
    VLOG(1) << "Arc is not enabled.";
    return false;
  }

  if (!profile) {
    VLOG(1) << "ARC is not supported for systems without profile.";
    return false;
  }

  if (profile->IsLegacySupervised()) {
    VLOG(1) << "Supervised users are not supported in ARC.";
    return false;
  }

  user_manager::User const* const user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(profile);
  if (!user || !user->HasGaiaAccount()) {
    VLOG(1) << "Users without GAIA accounts are not supported in ARC.";
    return false;
  }

  if (user_manager::UserManager::Get()
          ->IsCurrentUserCryptohomeDataEphemeral()) {
    VLOG(2) << "Users with ephemeral data are not supported in Arc.";
    return false;
  }

  return true;
}

void ArcAuthService::OnAuthInstanceReady() {
  arc_bridge_service()->auth_instance()->Init(
      binding_.CreateInterfacePtrAndBind());
}

std::string ArcAuthService::GetAndResetAuthCode() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  std::string auth_code;
  auth_code_.swap(auth_code);
  return auth_code;
}

void ArcAuthService::GetAuthCodeDeprecated(
    const GetAuthCodeDeprecatedCallback& callback) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(!IsOptInVerificationDisabled());
  callback.Run(mojo::String(GetAndResetAuthCode()));
}

void ArcAuthService::GetAuthCode(const GetAuthCodeCallback& callback) {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  const std::string auth_code = GetAndResetAuthCode();
  const bool verification_disabled = IsOptInVerificationDisabled();
  if (!auth_code.empty() || verification_disabled) {
    callback.Run(mojo::String(auth_code), !verification_disabled);
    return;
  }

  initial_opt_in_ = false;
  auth_callback_ = callback;
  StartUI();
}

void ArcAuthService::OnSignInComplete() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK_EQ(state_, State::ACTIVE);

  if (!IsOptInVerificationDisabled() &&
      !profile_->GetPrefs()->HasPrefPath(prefs::kArcSignedIn)) {
    playstore_launcher_.reset(
        new ArcAppLauncher(profile_, kPlayStoreAppId, true));
  }

  profile_->GetPrefs()->SetBoolean(prefs::kArcSignedIn, true);
  CloseUI();
}

void ArcAuthService::OnSignInFailed(arc::mojom::ArcSignInFailureReason reason) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK_EQ(state_, State::ACTIVE);

  int error_message_id;
  switch (reason) {
    case arc::mojom::ArcSignInFailureReason::NETWORK_ERROR:
      error_message_id = IDS_ARC_SIGN_IN_NETWORK_ERROR;
      UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
      break;
    case arc::mojom::ArcSignInFailureReason::SERVICE_UNAVAILABLE:
      error_message_id = IDS_ARC_SIGN_IN_SERVICE_UNAVAILABLE_ERROR;
      UpdateOptInCancelUMA(OptInCancelReason::SERVICE_UNAVAILABLE);
      break;
    case arc::mojom::ArcSignInFailureReason::BAD_AUTHENTICATION:
      error_message_id = IDS_ARC_SIGN_IN_BAD_AUTHENTICATION_ERROR;
      UpdateOptInCancelUMA(OptInCancelReason::BAD_AUTHENTICATION);
      break;
    case arc::mojom::ArcSignInFailureReason::GMS_CORE_NOT_AVAILABLE:
      error_message_id = IDS_ARC_SIGN_IN_GMS_NOT_AVAILABLE_ERROR;
      UpdateOptInCancelUMA(OptInCancelReason::GMS_CORE_NOT_AVAILABLE);
      break;
    case arc::mojom::ArcSignInFailureReason::CLOUD_PROVISION_FLOW_FAIL:
      error_message_id = IDS_ARC_SIGN_IN_CLOUD_PROVISION_FLOW_FAIL_ERROR;
      UpdateOptInCancelUMA(OptInCancelReason::CLOUD_PROVISION_FLOW_FAIL);
      break;
    default:
      error_message_id = IDS_ARC_SIGN_IN_UNKNOWN_ERROR;
      UpdateOptInCancelUMA(OptInCancelReason::UNKNOWN_ERROR);
  }

  if (profile_->GetPrefs()->HasPrefPath(prefs::kArcSignedIn))
    profile_->GetPrefs()->SetBoolean(prefs::kArcSignedIn, false);
  ShutdownBridgeAndShowUI(UIPage::ERROR,
                          l10n_util::GetStringUTF16(error_message_id));
}

void ArcAuthService::GetIsAccountManaged(
    const GetIsAccountManagedCallback& callback) {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  callback.Run(IsAccountManaged(profile_));
}

void ArcAuthService::SetState(State state) {
  if (state_ == state)
    return;

  state_ = state;
  FOR_EACH_OBSERVER(Observer, observer_list_, OnOptInChanged(state_));
}

bool ArcAuthService::IsAllowed() const {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  return profile_ != nullptr;
}

void ArcAuthService::OnPrimaryUserProfilePrepared(Profile* profile) {
  DCHECK(profile && profile != profile_);
  DCHECK(thread_checker.Get().CalledOnValidThread());

  Shutdown();

  profile_ = profile;
  SetState(State::STOPPED);

  if (!IsAllowedForProfile(profile))
    return;

  // TODO (khmel): Move this to IsAllowedForProfile.
  if (IsArcDisabledForEnterprise() && IsAccountManaged(profile)) {
    VLOG(2) << "Enterprise users are not supported in ARC.";
    return;
  }

  PrefServiceSyncableFromProfile(profile_)->AddSyncedPrefObserver(
      prefs::kArcEnabled, this);

  // Reuse storage used in ARC OptIn platform app.
  const std::string site_url = base::StringPrintf(
      "%s://%s/persist?%s", content::kGuestScheme, ArcSupportHost::kHostAppId,
      ArcSupportHost::kStorageId);
  storage_partition_ = content::BrowserContext::GetStoragePartitionForSite(
      profile_, GURL(site_url));
  CHECK(storage_partition_);

  // Get token service and account ID to fetch auth tokens.
  token_service_ = ProfileOAuth2TokenServiceFactory::GetForProfile(profile_);
  const SigninManagerBase* const signin_manager =
      SigninManagerFactory::GetForProfile(profile_);
  CHECK(token_service_ && signin_manager);
  account_id_ = signin_manager->GetAuthenticatedAccountId();

  // In case UI is disabled we assume that ARC is opted-in.
  if (!IsOptInVerificationDisabled()) {
    if (!disable_ui_for_testing || enable_check_android_management_for_testing)
      StartAndroidManagementClient();
    pref_change_registrar_.Init(profile_->GetPrefs());
    pref_change_registrar_.Add(
        prefs::kArcEnabled,
        base::Bind(&ArcAuthService::OnOptInPreferenceChanged,
                   weak_ptr_factory_.GetWeakPtr()));
    if (profile_->GetPrefs()->GetBoolean(prefs::kArcEnabled)) {
      OnOptInPreferenceChanged();
    } else {
      UpdateEnabledStateUMA(false);
      PrefServiceSyncableFromProfile(profile_)->AddObserver(this);
      OnIsSyncingChanged();
    }
  } else {
    auth_code_.clear();
    StartArc();
  }
}

void ArcAuthService::OnIsSyncingChanged() {
  syncable_prefs::PrefServiceSyncable* const pref_service_syncable =
      PrefServiceSyncableFromProfile(profile_);
  if (!pref_service_syncable->IsSyncing())
    return;

  pref_service_syncable->RemoveObserver(this);

  if (IsArcEnabled())
    OnOptInPreferenceChanged();

  if (!disable_ui_for_testing && profile_->IsNewProfile() &&
      !profile_->GetPrefs()->HasPrefPath(prefs::kArcEnabled)) {
    arc::ArcAuthNotification::Show();
  }
}

void ArcAuthService::Shutdown() {
  ShutdownBridgeAndCloseUI();
  if (profile_) {
    syncable_prefs::PrefServiceSyncable* pref_service_syncable =
        PrefServiceSyncableFromProfile(profile_);
    pref_service_syncable->RemoveObserver(this);
    pref_service_syncable->RemoveSyncedPrefObserver(prefs::kArcEnabled, this);
  }
  pref_change_registrar_.RemoveAll();
  profile_ = nullptr;
  SetState(State::NOT_INITIALIZED);
}

void ArcAuthService::ShowUI(UIPage page, const base::string16& status) {
  if (disable_ui_for_testing || IsOptInVerificationDisabled())
    return;

  SetUIPage(page, status);
  const extensions::AppWindowRegistry* const app_window_registry =
      extensions::AppWindowRegistry::Get(profile_);
  DCHECK(app_window_registry);
  if (app_window_registry->GetCurrentAppWindowForApp(
          ArcSupportHost::kHostAppId)) {
    return;
  }

  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile_)->GetInstalledExtension(
          ArcSupportHost::kHostAppId);
  CHECK(extension && extensions::util::IsAppLaunchable(
                         ArcSupportHost::kHostAppId, profile_));

  OpenApplication(CreateAppLaunchParamsUserContainer(
      profile_, extension, NEW_WINDOW, extensions::SOURCE_CHROME_INTERNAL));
}

void ArcAuthService::OnMergeSessionSuccess(const std::string& data) {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  DCHECK(!initial_opt_in_);
  context_prepared_ = true;
  CheckAndroidManagement();
}

void ArcAuthService::OnMergeSessionFailure(
    const GoogleServiceAuthError& error) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  VLOG(2) << "Failed to merge gaia session " << error.ToString() << ".";
  OnPrepareContextFailed();
}

void ArcAuthService::OnUbertokenSuccess(const std::string& token) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  merger_fetcher_.reset(
      new GaiaAuthFetcher(this, GaiaConstants::kChromeOSSource,
                          storage_partition_->GetURLRequestContext()));
  merger_fetcher_->StartMergeSession(token, std::string());
}

void ArcAuthService::OnUbertokenFailure(const GoogleServiceAuthError& error) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  VLOG(2) << "Failed to get ubertoken " << error.ToString() << ".";
  OnPrepareContextFailed();
}

void ArcAuthService::OnSyncedPrefChanged(const std::string& path,
                                         bool from_sync) {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  // Update UMA only for local changes
  if (!from_sync) {
    UpdateOptInActionUMA(profile_->GetPrefs()->GetBoolean(prefs::kArcEnabled)
                             ? OptInActionType::OPTED_IN
                             : OptInActionType::OPTED_OUT);
  }
}

void ArcAuthService::OnOptInPreferenceChanged() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(profile_);

  const bool arc_enabled = IsArcEnabled();
  FOR_EACH_OBSERVER(Observer, observer_list_, OnOptInEnabled(arc_enabled));

  if (!arc_enabled) {
    if (state_ != State::STOPPED)
      UpdateEnabledStateUMA(false);
    ShutdownBridgeAndCloseUI();
    return;
  }

  if (state_ == State::ACTIVE)
    return;
  CloseUI();
  auth_code_.clear();

  if (!profile_->GetPrefs()->GetBoolean(prefs::kArcSignedIn)) {
    // Need pre-fetch auth code and show OptIn UI if needed.
    initial_opt_in_ = true;
    StartUI();
  } else {
    // Ready to start Arc, but check Android management first.
    if (!disable_ui_for_testing ||
        enable_check_android_management_for_testing) {
      CheckAndroidManagement();
    } else {
      StartArc();
    }
  }

  UpdateEnabledStateUMA(true);
}

void ArcAuthService::ShutdownBridge() {
  playstore_launcher_.reset();
  auth_callback_.reset();
  ubertoken_fethcher_.reset();
  merger_fetcher_.reset();
  token_service_ = nullptr;
  account_id_ = "";
  arc_bridge_service()->Shutdown();
  if (state_ != State::NOT_INITIALIZED)
    SetState(State::STOPPED);
  FOR_EACH_OBSERVER(Observer, observer_list_, OnShutdownBridge());
}

void ArcAuthService::ShutdownBridgeAndCloseUI() {
  ShutdownBridge();
  CloseUI();
}

void ArcAuthService::ShutdownBridgeAndShowUI(UIPage page,
                                             const base::string16& status) {
  ShutdownBridge();
  ShowUI(page, status);
}

void ArcAuthService::AddObserver(Observer* observer) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  observer_list_.AddObserver(observer);
}

void ArcAuthService::RemoveObserver(Observer* observer) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  observer_list_.RemoveObserver(observer);
}

void ArcAuthService::CloseUI() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnOptInUIClose());
  SetUIPage(UIPage::NO_PAGE, base::string16());
  if (!disable_ui_for_testing)
    ArcAuthNotification::Hide();
}

void ArcAuthService::SetUIPage(UIPage page, const base::string16& status) {
  ui_page_ = page;
  ui_page_status_ = status;
  FOR_EACH_OBSERVER(Observer, observer_list_,
                    OnOptInUIShowPage(ui_page_, ui_page_status_));
}

void ArcAuthService::StartArc() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  arc_bridge_service()->HandleStartup();
  SetState(State::ACTIVE);
}

void ArcAuthService::SetAuthCodeAndStartArc(const std::string& auth_code) {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(!auth_code.empty());

  if (!auth_callback_.is_null()) {
    DCHECK_EQ(state_, State::FETCHING_CODE);
    SetState(State::ACTIVE);
    auth_callback_.Run(mojo::String(auth_code), !IsOptInVerificationDisabled());
    auth_callback_.reset();
    return;
  }

  State state = state_;
  if (state != State::FETCHING_CODE) {
    ShutdownBridgeAndCloseUI();
    return;
  }

  SetUIPage(UIPage::START_PROGRESS, base::string16());
  ShutdownBridge();
  auth_code_ = auth_code;
  StartArc();
}

void ArcAuthService::StartLso() {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  // Update UMA only if error is currently shown.
  if (ui_page_ == UIPage::ERROR)
    UpdateOptInActionUMA(OptInActionType::RETRY);

  initial_opt_in_ = false;
  StartUI();
}

void ArcAuthService::CancelAuthCode() {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  if (state_ != State::FETCHING_CODE && ui_page_ != UIPage::ERROR)
    return;

  // Update UMA with user cancel only if error is not currently shown.
  if (ui_page_ != UIPage::ERROR && ui_page_ != UIPage::NO_PAGE)
    UpdateOptInCancelUMA(OptInCancelReason::USER_CANCEL);

  DisableArc();
}

bool ArcAuthService::IsArcEnabled() const {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(profile_);
  return profile_->GetPrefs()->GetBoolean(prefs::kArcEnabled);
}

void ArcAuthService::EnableArc() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(profile_);
  profile_->GetPrefs()->SetBoolean(prefs::kArcEnabled, true);
}

void ArcAuthService::DisableArc() {
  DCHECK(thread_checker.Get().CalledOnValidThread());
  DCHECK(profile_);
  profile_->GetPrefs()->SetBoolean(prefs::kArcEnabled, false);
}

void ArcAuthService::PrepareContext() {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  ubertoken_fethcher_.reset(
      new UbertokenFetcher(token_service_, this, GaiaConstants::kChromeOSSource,
                           storage_partition_->GetURLRequestContext()));
  ubertoken_fethcher_->StartFetchingToken(account_id_);
}

void ArcAuthService::StartUI() {
  DCHECK(thread_checker.Get().CalledOnValidThread());

  SetState(State::FETCHING_CODE);

  if (initial_opt_in_) {
    initial_opt_in_ = false;
    ShowUI(UIPage::START, base::string16());
  } else if (context_prepared_) {
    CheckAndroidManagement();
  } else {
    PrepareContext();
  }
}

void ArcAuthService::OnPrepareContextFailed() {
  DCHECK_EQ(state_, State::FETCHING_CODE);

  ShutdownBridgeAndShowUI(
      UIPage::ERROR,
      l10n_util::GetStringUTF16(IDS_ARC_SERVER_COMMUNICATION_ERROR));
  UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
}

void ArcAuthService::StartAndroidManagementClient() {
  policy::BrowserPolicyConnectorChromeOS* const connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  policy::DeviceManagementService* const service =
      connector->device_management_service();
  service->ScheduleInitialization(0);
  android_management_client_.reset(new policy::AndroidManagementClient(
      service, g_browser_process->system_request_context(), account_id_,
      token_service_));
}

void ArcAuthService::CheckAndroidManagement() {
  // Do not send requests for Chrome OS managed users.
  if (IsAccountManaged(profile_)) {
    StartArcIfSignedIn();
    return;
  }

  // Do not send requests for well-known consumer domains.
  if (policy::BrowserPolicyConnector::IsNonEnterpriseUser(
          profile_->GetProfileUserName())) {
    StartArcIfSignedIn();
    return;
  }

  android_management_client_->StartCheckAndroidManagement(
      base::Bind(&ArcAuthService::OnAndroidManagementChecked,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ArcAuthService::OnAndroidManagementChecked(
    policy::AndroidManagementClient::Result result) {
  switch (result) {
    case policy::AndroidManagementClient::Result::RESULT_UNMANAGED:
      StartArcIfSignedIn();
      break;
    case policy::AndroidManagementClient::Result::RESULT_MANAGED:
      ShutdownBridgeAndShowUI(
          UIPage::ERROR,
          l10n_util::GetStringUTF16(IDS_ARC_ANDROID_MANAGEMENT_REQUIRED_ERROR));
      UpdateOptInCancelUMA(OptInCancelReason::ANDROID_MANAGEMENT_REQUIRED);
      break;
    case policy::AndroidManagementClient::Result::RESULT_ERROR:
      ShutdownBridgeAndShowUI(
          UIPage::ERROR,
          l10n_util::GetStringUTF16(IDS_ARC_SERVER_COMMUNICATION_ERROR));
      UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
      break;
    default:
      NOTREACHED();
  }
}

void ArcAuthService::StartArcIfSignedIn() {
  if (profile_->GetPrefs()->GetBoolean(prefs::kArcSignedIn) ||
      IsOptInVerificationDisabled()) {
    StartArc();
  } else {
    ShowUI(UIPage::LSO_PROGRESS, base::string16());
  }
}

std::ostream& operator<<(std::ostream& os, const ArcAuthService::State& state) {
  switch (state) {
    case ArcAuthService::State::NOT_INITIALIZED:
      return os << kStateNotInitialized;
    case ArcAuthService::State::STOPPED:
      return os << kStateStopped;
    case ArcAuthService::State::FETCHING_CODE:
      return os << kStateFetchingCode;
    case ArcAuthService::State::ACTIVE:
      return os << kStateActive;
    default:
      NOTREACHED();
      return os;
  }
}

}  // namespace arc
