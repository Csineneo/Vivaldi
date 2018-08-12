// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_bubble_experiment.h"

#include <string>

#include "base/metrics/field_trial.h"
#include "base/strings/string_number_conversions.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/variations/variations_associated_data.h"

namespace password_bubble_experiment {

const char kBrandingExperimentName[] = "PasswordBranding";
const char kSmartBubbleExperimentName[] = "PasswordSmartBubble";
const char kSmartBubbleThresholdParam[] = "dismissal_count";
const char kSmartLockBrandingGroupName[] = "SmartLockBranding";
const char kSmartLockBrandingSavePromptOnlyGroupName[] =
    "SmartLockBrandingSavePromptOnly";

void RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(
      password_manager::prefs::kWasSavePrompFirstRunExperienceShown, false);

  registry->RegisterBooleanPref(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF);
}

int GetSmartBubbleDismissalThreshold() {
  std::string param = variations::GetVariationParamValue(
      kSmartBubbleExperimentName, kSmartBubbleThresholdParam);
  int threshold = 0;
  return base::StringToInt(param, &threshold) ? threshold : 0;
}

bool IsSmartLockUser(const sync_driver::SyncService* sync_service) {
  return password_manager_util::GetPasswordSyncState(sync_service) ==
         password_manager::SYNCING_NORMAL_ENCRYPTION;
}

SmartLockBranding GetSmartLockBrandingState(
    const sync_driver::SyncService* sync_service) {
  // Query the group first for correct UMA reporting.
  std::string group_name =
      base::FieldTrialList::FindFullName(kBrandingExperimentName);
  if (!IsSmartLockUser(sync_service))
    return SmartLockBranding::NONE;
  if (group_name == kSmartLockBrandingGroupName)
    return SmartLockBranding::FULL;
  if (group_name == kSmartLockBrandingSavePromptOnlyGroupName)
    return SmartLockBranding::SAVE_PROMPT_ONLY;
  return SmartLockBranding::NONE;
}

bool IsSmartLockBrandingEnabled(const sync_driver::SyncService* sync_service) {
  return GetSmartLockBrandingState(sync_service) == SmartLockBranding::FULL;
}

bool IsSmartLockBrandingSavePromptEnabled(
    const sync_driver::SyncService* sync_service) {
  return GetSmartLockBrandingState(sync_service) != SmartLockBranding::NONE;
}

bool ShouldShowSavePromptFirstRunExperience(
    const sync_driver::SyncService* sync_service,
    PrefService* prefs) {
  return false;
}

void RecordSavePromptFirstRunExperienceWasShown(PrefService* prefs) {
  prefs->SetBoolean(
      password_manager::prefs::kWasSavePrompFirstRunExperienceShown, true);
}

bool ShouldShowAutoSignInPromptFirstRunExperience(PrefService* prefs) {
  return !prefs->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown);
}

void RecordAutoSignInPromptFirstRunExperienceWasShown(PrefService* prefs) {
  prefs->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, true);
}

void TurnOffAutoSignin(PrefService* prefs) {
  prefs->SetBoolean(password_manager::prefs::kCredentialsEnableAutosignin,
                    false);
}

}  // namespace password_bubble_experiment
