// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/arc_auth_service.h"

#include <utility>

#include "ash/common/shelf/shelf_delegate.h"
#include "ash/common/wm_shell.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/arc/arc_auth_code_fetcher.h"
#include "chrome/browser/chromeos/arc/arc_auth_context.h"
#include "chrome/browser/chromeos/arc/arc_auth_notification.h"
#include "chrome/browser/chromeos/arc/arc_optin_uma.h"
#include "chrome/browser/chromeos/arc/arc_support_host.h"
#include "chrome/browser/chromeos/arc/auth/arc_robot_auth.h"
#include "chrome/browser/chromeos/arc/optin/arc_optin_preference_handler.h"
#include "chrome/browser/chromeos/arc/policy/arc_android_management_checker.h"
#include "chrome/browser/chromeos/arc/policy/arc_policy_util.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "chrome/browser/prefs/pref_service_syncable_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_launcher.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/arc_features.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "components/user_manager/user.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/browser/extension_prefs.h"

namespace arc {

namespace {

constexpr size_t kMinVersionForOnAccountInfoReady = 5;

// Weak pointer.  This class is owned by ArcServiceManager.
ArcAuthService* g_arc_auth_service = nullptr;

// Skip creating UI in unit tests
bool g_disable_ui_for_testing = false;

// Use specified ash::ShelfDelegate for unit tests.
ash::ShelfDelegate* g_shelf_delegate_for_testing = nullptr;

// The Android management check is disabled by default, it's used only for
// testing.
bool g_enable_check_android_management_for_testing = false;

// Maximum amount of time we'll wait for ARC to finish booting up. Once this
// timeout expires, keep ARC running in case the user wants to file feedback,
// but present the UI to try again.
constexpr base::TimeDelta kArcSignInTimeout = base::TimeDelta::FromMinutes(5);

ash::ShelfDelegate* GetShelfDelegate() {
  if (g_shelf_delegate_for_testing)
    return g_shelf_delegate_for_testing;
  if (ash::WmShell::HasInstance()) {
    DCHECK(ash::WmShell::Get()->shelf_delegate());
    return ash::WmShell::Get()->shelf_delegate();
  }
  return nullptr;
}

ProvisioningResult ConvertArcSignInFailureReasonToProvisioningResult(
    mojom::ArcSignInFailureReason reason) {
  using ArcSignInFailureReason = mojom::ArcSignInFailureReason;

#define MAP_PROVISIONING_RESULT(name) \
  case ArcSignInFailureReason::name:  \
    return ProvisioningResult::name

  switch (reason) {
    MAP_PROVISIONING_RESULT(UNKNOWN_ERROR);
    MAP_PROVISIONING_RESULT(MOJO_VERSION_MISMATCH);
    MAP_PROVISIONING_RESULT(MOJO_CALL_TIMEOUT);
    MAP_PROVISIONING_RESULT(DEVICE_CHECK_IN_FAILED);
    MAP_PROVISIONING_RESULT(DEVICE_CHECK_IN_TIMEOUT);
    MAP_PROVISIONING_RESULT(DEVICE_CHECK_IN_INTERNAL_ERROR);
    MAP_PROVISIONING_RESULT(GMS_NETWORK_ERROR);
    MAP_PROVISIONING_RESULT(GMS_SERVICE_UNAVAILABLE);
    MAP_PROVISIONING_RESULT(GMS_BAD_AUTHENTICATION);
    MAP_PROVISIONING_RESULT(GMS_SIGN_IN_FAILED);
    MAP_PROVISIONING_RESULT(GMS_SIGN_IN_TIMEOUT);
    MAP_PROVISIONING_RESULT(GMS_SIGN_IN_INTERNAL_ERROR);
    MAP_PROVISIONING_RESULT(CLOUD_PROVISION_FLOW_FAILED);
    MAP_PROVISIONING_RESULT(CLOUD_PROVISION_FLOW_TIMEOUT);
    MAP_PROVISIONING_RESULT(CLOUD_PROVISION_FLOW_INTERNAL_ERROR);
  }
#undef MAP_PROVISIONING_RESULT

  NOTREACHED() << "unknown reason: " << static_cast<int>(reason);
  return ProvisioningResult::UNKNOWN_ERROR;
}

bool IsArcKioskMode() {
  return user_manager::UserManager::Get()->IsLoggedInAsArcKioskApp();
}

mojom::ChromeAccountType GetAccountType() {
  if (IsArcKioskMode())
    return mojom::ChromeAccountType::ROBOT_ACCOUNT;
  return mojom::ChromeAccountType::USER_ACCOUNT;
}

}  // namespace

// TODO(lhchavez): Get rid of this class once we can safely remove all the
// deprecated interfaces and only need to care about one type of callback.
class ArcAuthService::AccountInfoNotifier {
 public:
  explicit AccountInfoNotifier(
      const GetAuthCodeDeprecatedCallback& auth_callback)
      : callback_type_(CallbackType::AUTH_CODE),
        auth_callback_(auth_callback) {}

  explicit AccountInfoNotifier(
      const GetAuthCodeAndAccountTypeDeprecatedCallback& auth_account_callback)
      : callback_type_(CallbackType::AUTH_CODE_AND_ACCOUNT),
        auth_account_callback_(auth_account_callback) {}

  explicit AccountInfoNotifier(const AccountInfoCallback& account_info_callback)
      : callback_type_(CallbackType::ACCOUNT_INFO),
        account_info_callback_(account_info_callback) {}

  void Notify(bool is_enforced,
              const std::string& auth_code,
              mojom::ChromeAccountType account_type,
              bool is_managed) {
    switch (callback_type_) {
      case CallbackType::AUTH_CODE:
        DCHECK(!auth_callback_.is_null());
        auth_callback_.Run(auth_code, is_enforced);
        break;
      case CallbackType::AUTH_CODE_AND_ACCOUNT:
        DCHECK(!auth_account_callback_.is_null());
        auth_account_callback_.Run(auth_code, is_enforced, account_type);
        break;
      case CallbackType::ACCOUNT_INFO:
        DCHECK(!account_info_callback_.is_null());
        mojom::AccountInfoPtr account_info = mojom::AccountInfo::New();
        if (!is_enforced) {
          account_info->auth_code = base::nullopt;
        } else {
          account_info->auth_code = auth_code;
        }
        account_info->account_type = account_type;
        account_info->is_managed = is_managed;
        account_info_callback_.Run(std::move(account_info));
        break;
    }
  }

 private:
  enum class CallbackType { AUTH_CODE, AUTH_CODE_AND_ACCOUNT, ACCOUNT_INFO };

  const CallbackType callback_type_;
  const GetAuthCodeDeprecatedCallback auth_callback_;
  const GetAuthCodeAndAccountTypeDeprecatedCallback auth_account_callback_;
  const AccountInfoCallback account_info_callback_;
};

ArcAuthService::ArcAuthService(ArcBridgeService* bridge_service)
    : ArcService(bridge_service), binding_(this), weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!g_arc_auth_service);

  g_arc_auth_service = this;

  arc_bridge_service()->AddObserver(this);
  arc_bridge_service()->auth()->AddObserver(this);
}

ArcAuthService::~ArcAuthService() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK_EQ(this, g_arc_auth_service);

  Shutdown();
  arc_bridge_service()->auth()->RemoveObserver(this);
  arc_bridge_service()->RemoveObserver(this);

  g_arc_auth_service = nullptr;
}

// static
ArcAuthService* ArcAuthService::Get() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return g_arc_auth_service;
}

// static
void ArcAuthService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  // TODO(dspaid): Implement a mechanism to allow this to sync on first boot
  // only.
  registry->RegisterBooleanPref(prefs::kArcDataRemoveRequested, false);
  registry->RegisterBooleanPref(prefs::kArcEnabled, false);
  registry->RegisterBooleanPref(prefs::kArcSignedIn, false);
  registry->RegisterBooleanPref(prefs::kArcTermsAccepted, false);
  registry->RegisterBooleanPref(prefs::kArcBackupRestoreEnabled, true);
  registry->RegisterBooleanPref(prefs::kArcLocationServiceEnabled, true);
}

// static
void ArcAuthService::DisableUIForTesting() {
  g_disable_ui_for_testing = true;
}

// static
void ArcAuthService::SetShelfDelegateForTesting(
    ash::ShelfDelegate* shelf_delegate) {
  g_shelf_delegate_for_testing = shelf_delegate;
}

// static
bool ArcAuthService::IsOptInVerificationDisabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kDisableArcOptInVerification);
}

// static
void ArcAuthService::EnableCheckAndroidManagementForTesting() {
  g_enable_check_android_management_for_testing = true;
}

// static
bool ArcAuthService::IsAllowedForProfile(const Profile* profile) {
  if (!ArcBridgeService::GetEnabled(base::CommandLine::ForCurrentProcess())) {
    VLOG(1) << "Arc is not enabled.";
    return false;
  }

  if (!profile) {
    VLOG(1) << "ARC is not supported for systems without profile.";
    return false;
  }

  if (!chromeos::ProfileHelper::IsPrimaryProfile(profile)) {
    VLOG(1) << "Non-primary users are not supported in ARC.";
    return false;
  }

  if (profile->IsLegacySupervised()) {
    VLOG(1) << "Supervised users are not supported in ARC.";
    return false;
  }

  user_manager::User const* const user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(profile);
  if ((!user || !user->HasGaiaAccount()) && !IsArcKioskMode()) {
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

void ArcAuthService::OnInstanceReady() {
  auto* instance = arc_bridge_service()->auth()->GetInstanceForMethod("Init");
  DCHECK(instance);
  instance->Init(binding_.CreateInterfacePtrAndBind());
}

void ArcAuthService::OnBridgeStopped(ArcBridgeService::StopReason reason) {
  // TODO(crbug.com/625923): Use |reason| to report more detailed errors.
  if (arc_sign_in_timer_.IsRunning()) {
    OnProvisioningFinished(ProvisioningResult::ARC_STOPPED);
  }

  if (profile_->GetPrefs()->GetBoolean(prefs::kArcDataRemoveRequested)) {
    // This should be always true, but just in case as this is looked at
    // inside RemoveArcData() at first.
    DCHECK(arc_bridge_service()->stopped());
    RemoveArcData();
  } else {
    // To support special "Stop and enable ARC" procedure for enterprise,
    // here call MaybeReenableArc() asyncronously.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&ArcAuthService::MaybeReenableArc,
                              weak_ptr_factory_.GetWeakPtr()));
  }
}

void ArcAuthService::RemoveArcData() {
  // Ignore redundant data removal request.
  if (state() == State::REMOVING_DATA_DIR)
    return;

  // OnArcDataRemoved resets this flag.
  profile_->GetPrefs()->SetBoolean(prefs::kArcDataRemoveRequested, true);

  if (!arc_bridge_service()->stopped()) {
    // Just set a flag. On bridge stopped, this will be re-called,
    // then session manager should remove the data.
    return;
  }

  SetState(State::REMOVING_DATA_DIR);
  chromeos::DBusThreadManager::Get()->GetSessionManagerClient()->RemoveArcData(
      cryptohome::Identification(
          multi_user_util::GetAccountIdFromProfile(profile_)),
      base::Bind(&ArcAuthService::OnArcDataRemoved,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ArcAuthService::OnArcDataRemoved(bool success) {
  LOG_IF(ERROR, !success) << "Required ARC user data wipe failed.";

  // TODO(khmel): Browser tests may shutdown profile by itself. Update browser
  // tests and remove this check.
  if (state() == State::NOT_INITIALIZED)
    return;

  for (auto& observer : observer_list_)
    observer.OnArcDataRemoved();

  profile_->GetPrefs()->SetBoolean(prefs::kArcDataRemoveRequested, false);
  DCHECK_EQ(state(), State::REMOVING_DATA_DIR);
  SetState(State::STOPPED);

  MaybeReenableArc();
}

void ArcAuthService::MaybeReenableArc() {
  // Here check if |reenable_arc_| is marked or not.
  // The only case this happens should be in the special case for enterprise
  // "on managed lost" case. In that case, OnBridgeStopped() should trigger
  // the RemoveArcData(), then this.
  // TODO(hidehiko): Restructure the code.
  if (!reenable_arc_ || !IsArcEnabled())
    return;

  // Restart ARC anyway. Let the enterprise reporting instance decide whether
  // the ARC user data wipe is still required or not.
  reenable_arc_ = false;
  VLOG(1) << "Reenable ARC";
  EnableArc();
}

void ArcAuthService::GetAuthCodeDeprecated0(
    const GetAuthCodeDeprecated0Callback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  NOTREACHED() << "GetAuthCodeDeprecated0() should no longer be callable";
}

void ArcAuthService::GetAuthCodeDeprecated(
    const GetAuthCodeDeprecatedCallback& callback) {
  // For robot account we must use RequestAccountInfo because it allows
  // to specify account type.
  DCHECK(!IsArcKioskMode());
  RequestAccountInfoInternal(
      base::MakeUnique<ArcAuthService::AccountInfoNotifier>(callback));
}

void ArcAuthService::GetAuthCodeAndAccountTypeDeprecated(
    const GetAuthCodeAndAccountTypeDeprecatedCallback& callback) {
  RequestAccountInfoInternal(
      base::MakeUnique<ArcAuthService::AccountInfoNotifier>(callback));
}

void ArcAuthService::RequestAccountInfo() {
  RequestAccountInfoInternal(
      base::MakeUnique<ArcAuthService::AccountInfoNotifier>(
          base::Bind(&ArcAuthService::OnAccountInfoReady,
                     weak_ptr_factory_.GetWeakPtr())));
}

void ArcAuthService::OnAccountInfoReady(mojom::AccountInfoPtr account_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto* instance = arc_bridge_service()->auth()->GetInstanceForMethod(
      "OnAccountInfoReady", kMinVersionForOnAccountInfoReady);
  DCHECK(instance);
  instance->OnAccountInfoReady(std::move(account_info));
}

void ArcAuthService::RequestAccountInfoInternal(
    std::unique_ptr<ArcAuthService::AccountInfoNotifier>
        account_info_notifier) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // No other auth code-related operation may be in progress.
  DCHECK(!account_info_notifier_);

  if (IsOptInVerificationDisabled()) {
    account_info_notifier->Notify(false /* = is_enforced */, std::string(),
                                  GetAccountType(),
                                  policy_util::IsAccountManaged(profile_));
    return;
  }

  // Hereafter asynchronous operation. Remember the notifier.
  account_info_notifier_ = std::move(account_info_notifier);

  // In Kiosk mode, use Robot auth code fetching.
  if (IsArcKioskMode()) {
    arc_robot_auth_.reset(new ArcRobotAuth());
    arc_robot_auth_->FetchRobotAuthCode(
        base::Bind(&ArcAuthService::OnRobotAuthCodeFetched,
                   weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  // Optionally retrive auth code in silent mode.
  if (base::FeatureList::IsEnabled(arc::kArcUseAuthEndpointFeature)) {
    DCHECK(!auth_code_fetcher_);
    auth_code_fetcher_ =
        base::MakeUnique<ArcAuthCodeFetcher>(profile_, context_.get());
    auth_code_fetcher_->Fetch(base::Bind(&ArcAuthService::OnAuthCodeFetched,
                                         weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  // Report that silent auth code is not activated. All other states are
  // reported in ArcBackgroundAuthCodeFetcher.
  UpdateSilentAuthCodeUMA(OptInSilentAuthCode::DISABLED);

  // Otherwise, show LSO page to user after HTTP context preparation, and let
  // them click "Sign in" button.
  DCHECK(context_);
  context_->Prepare(base::Bind(&ArcAuthService::OnContextPrepared,
                               weak_ptr_factory_.GetWeakPtr()));
}

void ArcAuthService::OnContextPrepared(
    net::URLRequestContextGetter* request_context_getter) {
  if (!support_host_)
    return;

  if (request_context_getter) {
    support_host_->ShowLso();
  } else {
    UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
    support_host_->ShowError(ArcSupportHost::Error::SIGN_IN_NETWORK_ERROR,
                             false);
  }
}

void ArcAuthService::OnRobotAuthCodeFetched(
    const std::string& robot_auth_code) {
  // We fetching robot auth code for ARC kiosk only.
  DCHECK(IsArcKioskMode());

  // Current instance of ArcRobotAuth became useless.
  arc_robot_auth_.reset();

  if (robot_auth_code.empty()) {
    VLOG(1) << "Robot account auth code fetching error";
    // Log out the user. All the cleanup will be done in Shutdown() method.
    // The callback is not called because auth code is empty.
    chrome::AttemptUserExit();
    return;
  }

  OnAuthCodeObtained(robot_auth_code);
}

void ArcAuthService::OnAuthCodeFetched(const std::string& auth_code) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auth_code_fetcher_.reset();

  if (auth_code.empty()) {
    OnProvisioningFinished(
        ProvisioningResult::CHROME_SERVER_COMMUNICATION_ERROR);
    return;
  }

  OnAuthCodeObtained(auth_code);
}

void ArcAuthService::OnSignInComplete() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK_EQ(state_, State::ACTIVE);
  OnProvisioningFinished(ProvisioningResult::SUCCESS);
}

void ArcAuthService::OnSignInFailed(mojom::ArcSignInFailureReason reason) {
  OnProvisioningFinished(
      ConvertArcSignInFailureReasonToProvisioningResult(reason));
}

void ArcAuthService::OnProvisioningFinished(ProvisioningResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // If the Mojo message to notify finishing the provisioning is already sent
  // from the container, it will be processed even after requesting to stop the
  // container. Ignore all |result|s arriving while ARC is disabled, in order to
  // avoid popping up an error message triggered below. This code intentionally
  // does not support the case of reenabling.
  if (!IsArcEnabled()) {
    LOG(WARNING) << "Provisioning result received after Arc was disabled. "
                 << "Ignoring result " << static_cast<int>(result) << ".";
    return;
  }

  // Due asynchronous nature of stopping Arc bridge, OnProvisioningFinished may
  // arrive after setting the |State::STOPPED| state and |State::Active| is not
  // guaranty set here. prefs::kArcDataRemoveRequested is also can be active
  // for now.

  if (provisioning_reported_) {
    // We don't expect ProvisioningResult::SUCCESS is reported twice or reported
    // after an error.
    DCHECK_NE(result, ProvisioningResult::SUCCESS);
    // TODO (khmel): Consider changing LOG to NOTREACHED once we guaranty that
    // no double message can happen in production.
    LOG(WARNING) << " Provisioning result was already reported. Ignoring "
                 << " additional result " << static_cast<int>(result) << ".";
    return;
  }
  provisioning_reported_ = true;

  if (result == ProvisioningResult::CHROME_SERVER_COMMUNICATION_ERROR) {
    // For backwards compatibility, use NETWORK_ERROR for
    // CHROME_SERVER_COMMUNICATION_ERROR case.
    UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
  } else if (!sign_in_time_.is_null()) {
    arc_sign_in_timer_.Stop();

    UpdateProvisioningTiming(base::Time::Now() - sign_in_time_,
                             result == ProvisioningResult::SUCCESS,
                             policy_util::IsAccountManaged(profile_));
    UpdateProvisioningResultUMA(result,
                                policy_util::IsAccountManaged(profile_));
    if (result != ProvisioningResult::SUCCESS)
      UpdateOptInCancelUMA(OptInCancelReason::CLOUD_PROVISION_FLOW_FAIL);
  }

  if (result == ProvisioningResult::SUCCESS) {
    if (support_host_)
      support_host_->Close();

    if (profile_->GetPrefs()->GetBoolean(prefs::kArcSignedIn))
      return;

    profile_->GetPrefs()->SetBoolean(prefs::kArcSignedIn, true);
    // Don't show Play Store app for ARC Kiosk because the only one UI in kiosk
    // mode must be the kiosk app and device is not needed for opt-in.
    if (!IsOptInVerificationDisabled() && !IsArcKioskMode()) {
      playstore_launcher_.reset(
          new ArcAppLauncher(profile_, kPlayStoreAppId, true));
    }

    for (auto& observer : observer_list_)
      observer.OnInitialStart();
    return;
  }

  ArcSupportHost::Error error;
  switch (result) {
    case ProvisioningResult::GMS_NETWORK_ERROR:
      error = ArcSupportHost::Error::SIGN_IN_NETWORK_ERROR;
      break;
    case ProvisioningResult::GMS_SERVICE_UNAVAILABLE:
    case ProvisioningResult::GMS_SIGN_IN_FAILED:
    case ProvisioningResult::GMS_SIGN_IN_TIMEOUT:
    case ProvisioningResult::GMS_SIGN_IN_INTERNAL_ERROR:
      error = ArcSupportHost::Error::SIGN_IN_SERVICE_UNAVAILABLE_ERROR;
      break;
    case ProvisioningResult::GMS_BAD_AUTHENTICATION:
      error = ArcSupportHost::Error::SIGN_IN_BAD_AUTHENTICATION_ERROR;
      break;
    case ProvisioningResult::DEVICE_CHECK_IN_FAILED:
    case ProvisioningResult::DEVICE_CHECK_IN_TIMEOUT:
    case ProvisioningResult::DEVICE_CHECK_IN_INTERNAL_ERROR:
      error = ArcSupportHost::Error::SIGN_IN_GMS_NOT_AVAILABLE_ERROR;
      break;
    case ProvisioningResult::CLOUD_PROVISION_FLOW_FAILED:
    case ProvisioningResult::CLOUD_PROVISION_FLOW_TIMEOUT:
    case ProvisioningResult::CLOUD_PROVISION_FLOW_INTERNAL_ERROR:
      error = ArcSupportHost::Error::SIGN_IN_CLOUD_PROVISION_FLOW_FAIL_ERROR;
      break;
    case ProvisioningResult::CHROME_SERVER_COMMUNICATION_ERROR:
      error = ArcSupportHost::Error::SERVER_COMMUNICATION_ERROR;
      break;
    default:
      error = ArcSupportHost::Error::SIGN_IN_UNKNOWN_ERROR;
      break;
  }

  if (result == ProvisioningResult::ARC_STOPPED ||
      result == ProvisioningResult::CHROME_SERVER_COMMUNICATION_ERROR) {
    if (profile_->GetPrefs()->HasPrefPath(prefs::kArcSignedIn))
      profile_->GetPrefs()->SetBoolean(prefs::kArcSignedIn, false);
    ShutdownBridge();
    if (support_host_)
      support_host_->ShowError(error, false);
    return;
  }

  if (result == ProvisioningResult::CLOUD_PROVISION_FLOW_FAILED ||
      result == ProvisioningResult::CLOUD_PROVISION_FLOW_TIMEOUT ||
      result == ProvisioningResult::CLOUD_PROVISION_FLOW_INTERNAL_ERROR ||
      // OVERALL_SIGN_IN_TIMEOUT might be an indication that ARC believes it is
      // fully setup, but Chrome does not.
      result == ProvisioningResult::OVERALL_SIGN_IN_TIMEOUT ||
      // Just to be safe, remove data if we don't know the cause.
      result == ProvisioningResult::UNKNOWN_ERROR) {
    RemoveArcData();
  }

  // We'll delay shutting down the bridge in this case to allow people to send
  // feedback.
  if (support_host_)
    support_host_->ShowError(error, true /* = show send feedback button */);
}

void ArcAuthService::GetIsAccountManagedDeprecated(
    const GetIsAccountManagedDeprecatedCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  callback.Run(policy_util::IsAccountManaged(profile_));
}

void ArcAuthService::SetState(State state) {
  state_ = state;
}

bool ArcAuthService::IsAllowed() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return profile_ != nullptr;
}

void ArcAuthService::OnPrimaryUserProfilePrepared(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile && profile != profile_);

  Shutdown();

  if (!IsAllowedForProfile(profile))
    return;

  // TODO(khmel): Move this to IsAllowedForProfile.
  if (policy_util::IsArcDisabledForEnterprise() &&
      policy_util::IsAccountManaged(profile)) {
    VLOG(2) << "Enterprise users are not supported in ARC.";
    return;
  }

  profile_ = profile;

  // Create the support host at initialization. Note that, practically,
  // ARC support Chrome app is rarely used (only opt-in and re-auth flow).
  // So, it may be better to initialize it lazily.
  // TODO(hidehiko): Revisit to think about lazy initialization.
  //
  // Don't show UI for ARC Kiosk because the only one UI in kiosk mode must
  // be the kiosk app. In case of error the UI will be useless as well, because
  // in typical use case there will be no one nearby the kiosk device, who can
  // do some action to solve the problem be means of UI.
  if (!g_disable_ui_for_testing && !IsOptInVerificationDisabled() &&
      !IsArcKioskMode()) {
    DCHECK(!support_host_);
    support_host_ = base::MakeUnique<ArcSupportHost>(profile_);
    support_host_->AddObserver(this);

    preference_handler_ = base::MakeUnique<arc::ArcOptInPreferenceHandler>(
        this, profile_->GetPrefs());
    // This automatically updates all preferences.
    preference_handler_->Start();
  }

  DCHECK_EQ(State::NOT_INITIALIZED, state_);
  SetState(State::STOPPED);

  PrefServiceSyncableFromProfile(profile_)->AddSyncedPrefObserver(
      prefs::kArcEnabled, this);

  context_.reset(new ArcAuthContext(profile_));

  if (!g_disable_ui_for_testing ||
      g_enable_check_android_management_for_testing) {
    ArcAndroidManagementChecker::StartClient();
  }
  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kArcEnabled, base::Bind(&ArcAuthService::OnOptInPreferenceChanged,
                                     weak_ptr_factory_.GetWeakPtr()));
  if (profile_->GetPrefs()->GetBoolean(prefs::kArcEnabled)) {
    // Don't start ARC if there is a pending request to remove the data. Restart
    // ARC once data removal finishes.
    if (profile_->GetPrefs()->GetBoolean(prefs::kArcDataRemoveRequested)) {
      reenable_arc_ = true;
      RemoveArcData();
    } else {
      OnOptInPreferenceChanged();
    }
  } else {
    RemoveArcData();
    PrefServiceSyncableFromProfile(profile_)->AddObserver(this);
    OnIsSyncingChanged();
  }
}

void ArcAuthService::OnIsSyncingChanged() {
  sync_preferences::PrefServiceSyncable* const pref_service_syncable =
      PrefServiceSyncableFromProfile(profile_);
  if (!pref_service_syncable->IsSyncing())
    return;

  pref_service_syncable->RemoveObserver(this);

  if (IsArcEnabled())
    OnOptInPreferenceChanged();

  if (!g_disable_ui_for_testing &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kEnableArcOOBEOptIn) &&
      profile_->IsNewProfile() &&
      !profile_->GetPrefs()->HasPrefPath(prefs::kArcEnabled)) {
    ArcAuthNotification::Show(profile_);
  }
}

void ArcAuthService::Shutdown() {
  if (!g_disable_ui_for_testing)
    ArcAuthNotification::Hide();

  ShutdownBridge();
  if (support_host_) {
    support_host_->Close();
    support_host_->RemoveObserver(this);
    support_host_.reset();
  }
  if (profile_) {
    sync_preferences::PrefServiceSyncable* pref_service_syncable =
        PrefServiceSyncableFromProfile(profile_);
    pref_service_syncable->RemoveObserver(this);
    pref_service_syncable->RemoveSyncedPrefObserver(prefs::kArcEnabled, this);
  }
  pref_change_registrar_.RemoveAll();
  context_.reset();
  profile_ = nullptr;
  arc_robot_auth_.reset();
  SetState(State::NOT_INITIALIZED);
}

void ArcAuthService::OnSyncedPrefChanged(const std::string& path,
                                         bool from_sync) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // Update UMA only for local changes
  if (!from_sync) {
    const bool arc_enabled =
        profile_->GetPrefs()->GetBoolean(prefs::kArcEnabled);
    UpdateOptInActionUMA(arc_enabled ? OptInActionType::OPTED_IN
                                     : OptInActionType::OPTED_OUT);

    if (!arc_enabled && !IsArcManaged()) {
      ash::ShelfDelegate* shelf_delegate = GetShelfDelegate();
      if (shelf_delegate)
        shelf_delegate->UnpinAppWithID(ArcSupportHost::kHostAppId);
    }
  }
}

void ArcAuthService::StopArc() {
  if (state_ != State::STOPPED) {
    profile_->GetPrefs()->SetBoolean(prefs::kArcSignedIn, false);
    profile_->GetPrefs()->SetBoolean(prefs::kArcTermsAccepted, false);
  }
  ShutdownBridge();
  if (support_host_)
    support_host_->Close();
}

void ArcAuthService::OnOptInPreferenceChanged() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile_);

  // TODO(dspaid): Move code from OnSyncedPrefChanged into this method.
  OnSyncedPrefChanged(prefs::kArcEnabled, IsArcManaged());

  const bool arc_enabled = IsArcEnabled();
  for (auto& observer : observer_list_)
    observer.OnOptInEnabled(arc_enabled);

  // Hide auth notification if it was opened before and arc.enabled pref was
  // explicitly set to true or false.
  if (!g_disable_ui_for_testing &&
      profile_->GetPrefs()->HasPrefPath(prefs::kArcEnabled)) {
    ArcAuthNotification::Hide();
  }

  if (!arc_enabled) {
    // Reset any pending request to re-enable Arc.
    reenable_arc_ = false;
    StopArc();
    RemoveArcData();
    return;
  }

  if (state_ == State::ACTIVE)
    return;

  if (state_ == State::REMOVING_DATA_DIR) {
    // Data removal request is in progress. Set flag to re-enable Arc once it is
    // finished.
    reenable_arc_ = true;
    return;
  }

  if (support_host_)
    support_host_->SetArcManaged(IsArcManaged());

  // In case UI is disabled we assume that ARC is opted-in. For ARC Kiosk we
  // skip ToS because it is very likely that near the device there will be
  // no one who is eligible to accept them. We skip if Android management check
  // because there are no managed human users for Kiosk exist.
  if (IsOptInVerificationDisabled() || IsArcKioskMode()) {
    // Automatically accept terms in kiosk mode. This is not required for
    // IsOptInVerificationDisabled mode because in last case it may cause
    // a privacy issue on next run without this flag set.
    if (IsArcKioskMode())
      profile_->GetPrefs()->SetBoolean(prefs::kArcTermsAccepted, true);
    StartArc();
    return;
  }

  if (!profile_->GetPrefs()->GetBoolean(prefs::kArcSignedIn)) {
    if (profile_->GetPrefs()->GetBoolean(prefs::kArcTermsAccepted)) {
      StartArc();
    } else {
      // Need pre-fetch auth code and show OptIn UI if needed.
      StartUI();
    }
  } else {
    // Ready to start Arc, but check Android management in parallel.
    StartArc();
    // Note: Because the callback may be called in synchronous way (i.e. called
    // on the same stack), StartCheck() needs to be called *after* StartArc().
    // Otherwise, DisableArc() which may be called in
    // OnBackgroundAndroidManagementChecked() could be ignored.
    if (!g_disable_ui_for_testing ||
        g_enable_check_android_management_for_testing) {
      android_management_checker_.reset(new ArcAndroidManagementChecker(
          profile_, context_->token_service(), context_->account_id(),
          true /* retry_on_error */));
      android_management_checker_->StartCheck(
          base::Bind(&ArcAuthService::OnBackgroundAndroidManagementChecked,
                     weak_ptr_factory_.GetWeakPtr()));
    }
  }
}

void ArcAuthService::ShutdownBridge() {
  arc_sign_in_timer_.Stop();
  playstore_launcher_.reset();
  account_info_notifier_.reset();
  android_management_checker_.reset();
  auth_code_fetcher_.reset();
  arc_bridge_service()->RequestStop();
  if (state_ != State::NOT_INITIALIZED && state_ != State::REMOVING_DATA_DIR)
    SetState(State::STOPPED);
  for (auto& observer : observer_list_)
    observer.OnShutdownBridge();
}

void ArcAuthService::AddObserver(Observer* observer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  observer_list_.AddObserver(observer);
}

void ArcAuthService::RemoveObserver(Observer* observer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  observer_list_.RemoveObserver(observer);
}

// This is the special method to support enterprise mojo API.
// TODO(hidehiko): Remove this.
void ArcAuthService::StopAndEnableArc() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!arc_bridge_service()->stopped());
  reenable_arc_ = true;
  StopArc();
}

void ArcAuthService::StartArc() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // Arc must be started only if no pending data removal request exists.
  DCHECK(!profile_->GetPrefs()->GetBoolean(prefs::kArcDataRemoveRequested));

  provisioning_reported_ = false;

  arc_bridge_service()->RequestStart();
  SetState(State::ACTIVE);
}

void ArcAuthService::OnAuthCodeObtained(const std::string& auth_code) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!auth_code.empty());

  account_info_notifier_->Notify(!IsOptInVerificationDisabled(), auth_code,
                                 GetAccountType(),
                                 policy_util::IsAccountManaged(profile_));
  account_info_notifier_.reset();
}

void ArcAuthService::OnArcSignInTimeout() {
  LOG(ERROR) << "Timed out waiting for first sign in.";
  OnProvisioningFinished(ProvisioningResult::OVERALL_SIGN_IN_TIMEOUT);
}

void ArcAuthService::CancelAuthCode() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (state_ == State::NOT_INITIALIZED) {
    NOTREACHED();
    return;
  }

  // In case |state_| is ACTIVE, UI page can be ARC_LOADING (which means normal
  // ARC booting) or ERROR (in case ARC can not be started). If ARC is booting
  // normally don't stop it on progress close.
  if ((state_ != State::SHOWING_TERMS_OF_SERVICE &&
       state_ != State::CHECKING_ANDROID_MANAGEMENT) &&
      (!support_host_ ||
       support_host_->ui_page() != ArcSupportHost::UIPage::ERROR)) {
    return;
  }

  // Update UMA with user cancel only if error is not currently shown.
  if (support_host_ &&
      support_host_->ui_page() != ArcSupportHost::UIPage::NO_PAGE &&
      support_host_->ui_page() != ArcSupportHost::UIPage::ERROR) {
    UpdateOptInCancelUMA(OptInCancelReason::USER_CANCEL);
  }

  StopArc();

  if (IsArcManaged())
    return;

  DisableArc();
}

bool ArcAuthService::IsArcManaged() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile_);
  return profile_->GetPrefs()->IsManagedPreference(prefs::kArcEnabled);
}

bool ArcAuthService::IsArcEnabled() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!IsAllowed())
    return false;

  DCHECK(profile_);
  return profile_->GetPrefs()->GetBoolean(prefs::kArcEnabled);
}

void ArcAuthService::EnableArc() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile_);

  if (IsArcEnabled()) {
    OnOptInPreferenceChanged();
    return;
  }

  if (!IsArcManaged())
    profile_->GetPrefs()->SetBoolean(prefs::kArcEnabled, true);
}

void ArcAuthService::DisableArc() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile_);
  profile_->GetPrefs()->SetBoolean(prefs::kArcEnabled, false);
}

void ArcAuthService::RecordArcState() {
  // Only record Enabled state if ARC is allowed in the first place, so we do
  // not split the ARC population by devices that cannot run ARC.
  if (IsAllowed())
    UpdateEnabledStateUMA(IsArcEnabled());
}

void ArcAuthService::StartUI() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!arc_bridge_service()->stopped()) {
    // If the user attempts to re-enable ARC while the bridge is still running
    // the user should not be able to continue until the bridge has stopped.
    if (support_host_) {
      support_host_->ShowError(
          ArcSupportHost::Error::SIGN_IN_SERVICE_UNAVAILABLE_ERROR, false);
    }
    return;
  }

  SetState(State::SHOWING_TERMS_OF_SERVICE);
  if (support_host_)
    support_host_->ShowTermsOfService();
}

void ArcAuthService::StartArcAndroidManagementCheck() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(arc_bridge_service()->stopped());
  DCHECK(state_ == State::SHOWING_TERMS_OF_SERVICE ||
         state_ == State::CHECKING_ANDROID_MANAGEMENT);
  SetState(State::CHECKING_ANDROID_MANAGEMENT);

  android_management_checker_.reset(new ArcAndroidManagementChecker(
      profile_, context_->token_service(), context_->account_id(),
      false /* retry_on_error */));
  android_management_checker_->StartCheck(
      base::Bind(&ArcAuthService::OnAndroidManagementChecked,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ArcAuthService::OnAndroidManagementChecked(
    policy::AndroidManagementClient::Result result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK_EQ(state_, State::CHECKING_ANDROID_MANAGEMENT);

  switch (result) {
    case policy::AndroidManagementClient::Result::UNMANAGED:
      VLOG(1) << "Starting ARC for first sign in.";
      sign_in_time_ = base::Time::Now();
      arc_sign_in_timer_.Start(FROM_HERE, kArcSignInTimeout,
                               base::Bind(&ArcAuthService::OnArcSignInTimeout,
                                          weak_ptr_factory_.GetWeakPtr()));
      StartArc();
      break;
    case policy::AndroidManagementClient::Result::MANAGED:
      ShutdownBridge();
      if (support_host_) {
        support_host_->ShowError(
            ArcSupportHost::Error::ANDROID_MANAGEMENT_REQUIRED_ERROR, false);
      }
      UpdateOptInCancelUMA(OptInCancelReason::ANDROID_MANAGEMENT_REQUIRED);
      break;
    case policy::AndroidManagementClient::Result::ERROR:
      ShutdownBridge();
      if (support_host_) {
        support_host_->ShowError(
            ArcSupportHost::Error::SERVER_COMMUNICATION_ERROR, false);
      }
      UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
      break;
  }
}

void ArcAuthService::OnBackgroundAndroidManagementChecked(
    policy::AndroidManagementClient::Result result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  switch (result) {
    case policy::AndroidManagementClient::Result::UNMANAGED:
      // Do nothing. ARC should be started already.
      break;
    case policy::AndroidManagementClient::Result::MANAGED:
      DisableArc();
      break;
    case policy::AndroidManagementClient::Result::ERROR:
      // This code should not be reached. For background check,
      // retry_on_error should be set.
      NOTREACHED();
  }
}

void ArcAuthService::OnWindowClosed() {
  DCHECK(support_host_);
  CancelAuthCode();
}

void ArcAuthService::OnTermsAgreed(bool is_metrics_enabled,
                                   bool is_backup_and_restore_enabled,
                                   bool is_location_service_enabled) {
  DCHECK(support_host_);

  // Terms were accepted
  profile_->GetPrefs()->SetBoolean(prefs::kArcTermsAccepted, true);

  // Since this is ARC support's UI event callback, preference_handler_
  // should be always created (see OnPrimaryUserProfilePrepared()).
  // TODO(hidehiko): Simplify the logic with the code restructuring.
  DCHECK(preference_handler_);
  preference_handler_->EnableMetrics(is_metrics_enabled);
  preference_handler_->EnableBackupRestore(is_backup_and_restore_enabled);
  preference_handler_->EnableLocationService(is_location_service_enabled);
  support_host_->ShowArcLoading();
  StartArcAndroidManagementCheck();
}

void ArcAuthService::OnAuthSucceeded(const std::string& auth_code) {
  DCHECK(support_host_);
  OnAuthCodeObtained(auth_code);
}

void ArcAuthService::OnAuthFailed() {
  // Don't report via callback. Extension is already showing more detailed
  // information. Update only UMA here.
  UpdateOptInCancelUMA(OptInCancelReason::NETWORK_ERROR);
}

void ArcAuthService::OnRetryClicked() {
  DCHECK(support_host_);

  UpdateOptInActionUMA(OptInActionType::RETRY);

  // TODO(hidehiko): Simplify the retry logic.
  if (!profile_->GetPrefs()->GetBoolean(prefs::kArcTermsAccepted)) {
    // If the user has not yet agreed on Terms of Service, then show it.
    support_host_->ShowTermsOfService();
  } else if (support_host_->ui_page() == ArcSupportHost::UIPage::ERROR &&
             !arc_bridge_service()->stopped()) {
    // ERROR_WITH_FEEDBACK is set in OnSignInFailed(). In the case, stopping
    // ARC was postponed to contain its internal state into the report.
    // Here, on retry, stop it, then restart.
    DCHECK_EQ(State::ACTIVE, state_);
    support_host_->ShowArcLoading();
    ShutdownBridge();
    reenable_arc_ = true;
  } else if (state_ == State::ACTIVE) {
    // This happens when ARC support Chrome app reports an error on "Sign in"
    // page.
    DCHECK(context_);
    context_->Prepare(base::Bind(&ArcAuthService::OnContextPrepared,
                                 weak_ptr_factory_.GetWeakPtr()));
  } else {
    // Otherwise, we restart ARC. Note: this is the first boot case.
    // For second or later boot, either ERROR_WITH_FEEDBACK case or ACTIVE
    // case must hit.
    support_host_->ShowArcLoading();
    StartArcAndroidManagementCheck();
  }
}

void ArcAuthService::OnSendFeedbackClicked() {
  DCHECK(support_host_);
  chrome::OpenFeedbackDialog(nullptr);
}

void ArcAuthService::OnMetricsModeChanged(bool enabled, bool managed) {
  if (!support_host_)
    return;
  support_host_->SetMetricsPreferenceCheckbox(enabled, managed);
}

void ArcAuthService::OnBackupAndRestoreModeChanged(bool enabled, bool managed) {
  if (!support_host_)
    return;
  support_host_->SetBackupAndRestorePreferenceCheckbox(enabled, managed);
}

void ArcAuthService::OnLocationServicesModeChanged(bool enabled, bool managed) {
  if (!support_host_)
    return;
  support_host_->SetLocationServicesPreferenceCheckbox(enabled, managed);
}

std::ostream& operator<<(std::ostream& os, const ArcAuthService::State& state) {
  switch (state) {
    case ArcAuthService::State::NOT_INITIALIZED:
      return os << "NOT_INITIALIZED";
    case ArcAuthService::State::STOPPED:
      return os << "STOPPED";
    case ArcAuthService::State::SHOWING_TERMS_OF_SERVICE:
      return os << "SHOWING_TERMS_OF_SERVICE";
    case ArcAuthService::State::CHECKING_ANDROID_MANAGEMENT:
      return os << "CHECKING_ANDROID_MANAGEMENT";
    case ArcAuthService::State::REMOVING_DATA_DIR:
      return os << "REMOVING_DATA_DIR";
    case ArcAuthService::State::ACTIVE:
      return os << "ACTIVE";
  }

  // Some compiler reports an error even if all values of an enum-class are
  // covered indivisually in a switch statement.
  NOTREACHED();
  return os;
}

}  // namespace arc
