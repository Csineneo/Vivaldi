// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/core/browser/account_tracker_service.h"

#include "base/callback.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "base/profiler/scoped_tracker.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "components/signin/core/browser/signin_client.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/common/signin_pref_names.h"

namespace {

const char kAccountKeyPath[] = "account_id";
const char kAccountEmailPath[] = "email";
const char kAccountGaiaPath[] = "gaia";
const char kAccountHostedDomainPath[] = "hd";
const char kAccountFullNamePath[] = "full_name";
const char kAccountGivenNamePath[] = "given_name";
const char kAccountLocalePath[] = "locale";
const char kAccountPictureURLPath[] = "picture_url";
const char kAccountServiceFlagsPath[] = "service_flags";

}

// This must be a string which can never be a valid domain.
const char AccountTrackerService::kNoHostedDomainFound[] = "NO_HOSTED_DOMAIN";

// This must be a string which can never be a valid picture URL.
const char AccountTrackerService::kNoPictureURLFound[] = "NO_PICTURE_URL";

AccountTrackerService::AccountInfo::AccountInfo() {}
AccountTrackerService::AccountInfo::~AccountInfo() {}

bool AccountTrackerService::AccountInfo::IsValid() const {
  return !account_id.empty() && !email.empty() && !gaia.empty() &&
         !hosted_domain.empty() && !full_name.empty() && !given_name.empty() &&
         !locale.empty() && !picture_url.empty();
}


const char AccountTrackerService::kAccountInfoPref[] = "account_info";

AccountTrackerService::AccountTrackerService()
    : signin_client_(NULL) {}

AccountTrackerService::~AccountTrackerService() {
}

void AccountTrackerService::Initialize(SigninClient* signin_client) {
  DCHECK(signin_client);
  DCHECK(!signin_client_);
  signin_client_ = signin_client;
  LoadFromPrefs();
}

void AccountTrackerService::Shutdown() {
}

void AccountTrackerService::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void AccountTrackerService::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

std::vector<AccountTrackerService::AccountInfo>
AccountTrackerService::GetAccounts() const {
  std::vector<AccountInfo> accounts;

  for (std::map<std::string, AccountState>::const_iterator it =
           accounts_.begin();
       it != accounts_.end();
       ++it) {
    const AccountState& state = it->second;
    accounts.push_back(state.info);
  }
  return accounts;
}

AccountTrackerService::AccountInfo AccountTrackerService::GetAccountInfo(
    const std::string& account_id) {
  if (ContainsKey(accounts_, account_id))
    return accounts_[account_id].info;

  return AccountInfo();
}

AccountTrackerService::AccountInfo
AccountTrackerService::FindAccountInfoByGaiaId(
    const std::string& gaia_id) {
  if (!gaia_id.empty()) {
    for (std::map<std::string, AccountState>::const_iterator it =
             accounts_.begin();
         it != accounts_.end();
         ++it) {
      const AccountState& state = it->second;
      if (state.info.gaia == gaia_id)
        return state.info;
    }
  }

  return AccountInfo();
}

AccountTrackerService::AccountInfo
AccountTrackerService::FindAccountInfoByEmail(
    const std::string& email) {
  if (!email.empty()) {
    for (std::map<std::string, AccountState>::const_iterator it =
             accounts_.begin();
         it != accounts_.end();
         ++it) {
      const AccountState& state = it->second;
      if (gaia::AreEmailsSame(state.info.email, email))
        return state.info;
    }
  }

  return AccountInfo();
}

AccountTrackerService::AccountIdMigrationState
AccountTrackerService::GetMigrationState() {
  return GetMigrationState(signin_client_->GetPrefs());
}

// static
AccountTrackerService::AccountIdMigrationState
AccountTrackerService::GetMigrationState(PrefService* pref_service) {
  return static_cast<AccountTrackerService::AccountIdMigrationState>(
      pref_service->GetInteger(prefs::kAccountIdMigrationState));
}

void AccountTrackerService::NotifyAccountUpdated(const AccountState& state) {
  DCHECK(!state.info.gaia.empty());
  FOR_EACH_OBSERVER(
      Observer, observer_list_, OnAccountUpdated(state.info));
}

void AccountTrackerService::NotifyAccountUpdateFailed(
    const std::string& account_id) {
  FOR_EACH_OBSERVER(
      Observer, observer_list_, OnAccountUpdateFailed(account_id));
}

void AccountTrackerService::NotifyAccountRemoved(const AccountState& state) {
  DCHECK(!state.info.gaia.empty());
  FOR_EACH_OBSERVER(
      Observer, observer_list_, OnAccountRemoved(state.info));
}

void AccountTrackerService::StartTrackingAccount(
    const std::string& account_id) {
  if (!ContainsKey(accounts_, account_id)) {
    DVLOG(1) << "StartTracking " << account_id;
    AccountState state;
    state.info.account_id = account_id;
    accounts_.insert(make_pair(account_id, state));
  }

  // If the info is already available on the client, might as well use it.
  if (signin_client_->UpdateAccountInfo(&accounts_[account_id].info))
    SaveToPrefs(accounts_[account_id]);
}

void AccountTrackerService::StopTrackingAccount(const std::string& account_id) {
  DVLOG(1) << "StopTracking " << account_id;
  if (ContainsKey(accounts_, account_id)) {
    AccountState& state = accounts_[account_id];
    RemoveFromPrefs(state);
    if (!state.info.gaia.empty())
      NotifyAccountRemoved(state);

    accounts_.erase(account_id);
  }
}

void AccountTrackerService::SetAccountStateFromUserInfo(
    const std::string& account_id,
    const base::DictionaryValue* user_info,
    const std::vector<std::string>* service_flags) {
  DCHECK(ContainsKey(accounts_, account_id));
  AccountState& state = accounts_[account_id];

  std::string gaia_id;
  std::string email;
  if (user_info->GetString("id", &gaia_id) &&
      user_info->GetString("email", &email)) {
    state.info.gaia = gaia_id;
    state.info.email = email;

    std::string hosted_domain;
    if (user_info->GetString("hd", &hosted_domain) && !hosted_domain.empty()) {
      state.info.hosted_domain = hosted_domain;
    } else {
      state.info.hosted_domain = kNoHostedDomainFound;
    }

    user_info->GetString("name", &state.info.full_name);
    user_info->GetString("given_name", &state.info.given_name);
    user_info->GetString("locale", &state.info.locale);

    std::string picture_url;
    if(user_info->GetString("picture", &picture_url)) {
      state.info.picture_url = picture_url;
    } else {
      state.info.picture_url = kNoPictureURLFound;
    }

    state.info.service_flags = *service_flags;

    NotifyAccountUpdated(state);
    SaveToPrefs(state);
  }
}

void AccountTrackerService::LoadFromPrefs() {
  const base::ListValue* list =
      signin_client_->GetPrefs()->GetList(kAccountInfoPref);
  std::set<std::string> to_remove;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    const base::DictionaryValue* dict;
    if (list->GetDictionary(i, &dict)) {
      base::string16 value;
      if (dict->GetString(kAccountKeyPath, &value)) {
        std::string account_id = base::UTF16ToUTF8(value);

        // Ignore incorrectly persisted non-canonical account ids.
        if (account_id.find('@') != std::string::npos &&
            account_id != gaia::CanonicalizeEmail(account_id)) {
          to_remove.insert(account_id);
          continue;
        }

        StartTrackingAccount(account_id);
        AccountState& state = accounts_[account_id];

        if (dict->GetString(kAccountGaiaPath, &value))
          state.info.gaia = base::UTF16ToUTF8(value);
        if (dict->GetString(kAccountEmailPath, &value))
          state.info.email = base::UTF16ToUTF8(value);
        if (dict->GetString(kAccountHostedDomainPath, &value))
          state.info.hosted_domain = base::UTF16ToUTF8(value);
        if (dict->GetString(kAccountFullNamePath, &value))
          state.info.full_name = base::UTF16ToUTF8(value);
        if (dict->GetString(kAccountGivenNamePath, &value))
          state.info.given_name = base::UTF16ToUTF8(value);
        if (dict->GetString(kAccountLocalePath, &value))
          state.info.locale = base::UTF16ToUTF8(value);
        if (dict->GetString(kAccountPictureURLPath, &value))
          state.info.picture_url = base::UTF16ToUTF8(value);

        const base::ListValue* service_flags_list;
        if (dict->GetList(kAccountServiceFlagsPath, &service_flags_list)) {
          std::string flag;
          for(base::Value* flag: *service_flags_list) {
            std::string flag_string;
            if(flag->GetAsString(&flag_string)) {
              state.info.service_flags.push_back(flag_string);
            }
          }
        }

        if (state.info.IsValid())
          NotifyAccountUpdated(state);
      }
    }
  }

  // Remove any obsolete prefs.
  for (auto account_id : to_remove) {
    AccountState state;
    state.info.account_id = account_id;
    RemoveFromPrefs(state);
  }
}

void AccountTrackerService::SaveToPrefs(const AccountState& state) {
  if (!signin_client_->GetPrefs())
    return;

  base::DictionaryValue* dict = NULL;
  base::string16 account_id_16 = base::UTF8ToUTF16(state.info.account_id);
  ListPrefUpdate update(signin_client_->GetPrefs(), kAccountInfoPref);
  for(size_t i = 0; i < update->GetSize(); ++i, dict = NULL) {
    if (update->GetDictionary(i, &dict)) {
      base::string16 value;
      if (dict->GetString(kAccountKeyPath, &value) && value == account_id_16)
        break;
    }
  }

  if (!dict) {
    dict = new base::DictionaryValue();
    update->Append(dict);  // |update| takes ownership.
    dict->SetString(kAccountKeyPath, account_id_16);
  }

  dict->SetString(kAccountEmailPath, state.info.email);
  dict->SetString(kAccountGaiaPath, state.info.gaia);
  dict->SetString(kAccountHostedDomainPath, state.info.hosted_domain);
  dict->SetString(kAccountFullNamePath, state.info.full_name);
  dict->SetString(kAccountGivenNamePath, state.info.given_name);
  dict->SetString(kAccountLocalePath, state.info.locale);
  dict->SetString(kAccountPictureURLPath, state.info.picture_url);

  scoped_ptr<base::ListValue> service_flags_list;
  service_flags_list.reset(new base::ListValue);
  service_flags_list->AppendStrings(state.info.service_flags);

  dict->Set(kAccountServiceFlagsPath, service_flags_list.Pass());
}

void AccountTrackerService::RemoveFromPrefs(const AccountState& state) {
  if (!signin_client_->GetPrefs())
    return;

  base::string16 account_id_16 = base::UTF8ToUTF16(state.info.account_id);
  ListPrefUpdate update(signin_client_->GetPrefs(), kAccountInfoPref);
  for(size_t i = 0; i < update->GetSize(); ++i) {
    base::DictionaryValue* dict = NULL;
    if (update->GetDictionary(i, &dict)) {
      base::string16 value;
      if (dict->GetString(kAccountKeyPath, &value) && value == account_id_16) {
        update->Remove(i, NULL);
        break;
      }
    }
  }
}

std::string AccountTrackerService::PickAccountIdForAccount(
    const std::string& gaia,
    const std::string& email) {
  return PickAccountIdForAccount(signin_client_->GetPrefs(), gaia, email);
}

// static
std::string AccountTrackerService::PickAccountIdForAccount(
    PrefService* pref_service,
    const std::string& gaia,
    const std::string& email) {
  DCHECK(!gaia.empty() ||
      GetMigrationState(pref_service) == MIGRATION_NOT_STARTED);
  DCHECK(!email.empty());
  switch(GetMigrationState(pref_service)) {
    case MIGRATION_NOT_STARTED:
    case MIGRATION_IN_PROGRESS:
      // Some tests don't use a real email address.  To support these cases,
      // don't try to canonicalize these strings.
      return (email.find('@') == std::string::npos) ? email :
          gaia::CanonicalizeEmail(email);
    case MIGRATION_DONE:
      return gaia;
    default:
      NOTREACHED();
      return email;
  }
}

std::string AccountTrackerService::SeedAccountInfo(const std::string& gaia,
                                                   const std::string& email) {
  const std::string account_id = PickAccountIdForAccount(gaia, email);
  const bool already_exists = ContainsKey(accounts_, account_id);
  StartTrackingAccount(account_id);
  AccountState& state = accounts_[account_id];
  DCHECK(!already_exists || state.info.gaia.empty() || state.info.gaia == gaia);
  state.info.gaia = gaia;
  state.info.email = email;
  SaveToPrefs(state);

  DVLOG(1) << "AccountTrackerService::SeedAccountInfo"
           << " account_id=" << account_id
           << " gaia_id=" << gaia
           << " email=" << email;

  return account_id;
}

void AccountTrackerService::SeedAccountInfo(
    AccountTrackerService::AccountInfo info) {
  info.account_id = PickAccountIdForAccount(info.gaia, info.email);
  if (info.hosted_domain.empty()) {
    info.hosted_domain = kNoHostedDomainFound;
  }

  if(info.IsValid()) {
    if(!ContainsKey(accounts_, info.account_id)) {
      SeedAccountInfo(info.gaia, info.email);
    }

    AccountState& state = accounts_[info.account_id];
    state.info = info;
    NotifyAccountUpdated(state);
    SaveToPrefs(state);
  }
}
