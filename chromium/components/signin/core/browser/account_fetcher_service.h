// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_ACCOUNT_FETCHER_SERVICE_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_ACCOUNT_FETCHER_SERVICE_H_

#include <list>
#include <map>

#include "base/containers/scoped_ptr_hash_map.h"
#include "base/threading/non_thread_safe.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "google_apis/gaia/oauth2_token_service.h"

class AccountInfoFetcher;
class AccountTrackerService;
class OAuth2TokenService;
class RefreshTokenAnnotationRequest;
class SigninClient;

class AccountFetcherService : public KeyedService,
                              public OAuth2TokenService::Observer,
                              public base::NonThreadSafe {
 public:
  // Name of the preference that tracks the int64 representation of the last
  // time the AccountTrackerService was updated.
  static const char kLastUpdatePref[];

  AccountFetcherService();
  ~AccountFetcherService() override;

  void Initialize(SigninClient* signin_client,
                  OAuth2TokenService* token_service,
                  AccountTrackerService* account_tracker_service);

  // KeyedService implementation
  void Shutdown() override;

  // To be called after the Profile is fully initialized; permits network
  // calls to be executed.
  void EnableNetworkFetches();

  void StartFetchingInvalidAccounts();

  // Indicates if all user information has been fetched. If the result is false,
  // there are still unfininshed fetchers.
  virtual bool IsAllUserInfoFetched() const;

  void FetchUserInfoBeforeSignin(const std::string& account_id);

 protected:
  AccountTrackerService* account_tracker_service() const {
    return account_tracker_service_;
  }

 private:
  class AccountInfoFetcher;

  void LoadFromTokenService();
  void RefreshFromTokenService();
  void ScheduleNextRefreshFromTokenService();

  // Virtual so that tests can override the network fetching behaviour.
  virtual void StartFetchingUserInfo(const std::string& account_id);

  // Refreshes the AccountInfo associated with |account_id| if it's invalid or
  // if |force_remote_fetch| is true.
  void RefreshAccountInfo(
     const std::string& account_id, bool force_remote_fetch);

  void DeleteFetcher(AccountInfoFetcher* fetcher);

  // Virtual so that tests can override the network fetching behaviour.
  virtual void SendRefreshTokenAnnotationRequest(const std::string& account_id);
  void RefreshTokenAnnotationRequestDone(const std::string& account_id);

  // These methods are called by fetchers.
  void OnUserInfoFetchSuccess(AccountInfoFetcher* fetcher,
                             const base::DictionaryValue* user_info,
                             const std::vector<std::string>* service_flags);
  void OnUserInfoFetchFailure(AccountInfoFetcher* fetcher);

  // OAuth2TokenService::Observer implementation.
  void OnRefreshTokenAvailable(const std::string& account_id) override;
  void OnRefreshTokenRevoked(const std::string& account_id) override;
  void OnRefreshTokensLoaded() override;

  AccountTrackerService* account_tracker_service_;  // Not owned.
  OAuth2TokenService* token_service_;  // Not owned.
  SigninClient* signin_client_;  // Not owned.
  std::map<std::string, AccountInfoFetcher*> user_info_requests_;
  bool network_fetches_enabled_;
  std::list<std::string> pending_user_info_fetches_;
  base::Time last_updated_;
  base::OneShotTimer<AccountFetcherService> timer_;
  bool shutdown_called_;

  // Holds references to refresh token annotation requests keyed by account_id.
  base::ScopedPtrHashMap<std::string, scoped_ptr<RefreshTokenAnnotationRequest>>
      refresh_token_annotation_requests_;

  DISALLOW_COPY_AND_ASSIGN(AccountFetcherService);
};

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_ACCOUNT_FETCHER_SERVICE_H_
