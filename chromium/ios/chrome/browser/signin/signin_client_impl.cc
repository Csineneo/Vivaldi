// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/signin/signin_client_impl.h"

#include "base/command_line.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/metrics/metrics_service.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_cookie_changed_subscription.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "components/signin/core/common/signin_pref_names.h"
#include "components/signin/core/common/signin_switches.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/content_settings/cookie_settings_factory.h"
#include "ios/chrome/browser/signin/gaia_auth_fetcher_ios.h"
#include "ios/chrome/browser/web_data_service_factory.h"
#include "ios/public/provider/chrome/browser/browser_state/browser_state_info_cache.h"
#include "ios/public/provider/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/public/provider/chrome/browser/browser_state/chrome_browser_state_manager.h"
#include "ios/public/provider/chrome/browser/chrome_browser_provider.h"
#include "ios/public/provider/components/signin/browser/profile_oauth2_token_service_ios_provider.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/gurl.h"

namespace {
const char kEphemeralUserDeviceIDPrefix[] = "t_";
}

SigninClientImpl::SigninClientImpl(
    ios::ChromeBrowserState* browser_state,
    SigninErrorController* signin_error_controller)
    : OAuth2TokenService::Consumer("signin_client_impl"),
      browser_state_(browser_state),
      signin_error_controller_(signin_error_controller) {
  signin_error_controller_->AddObserver(this);
  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
}

SigninClientImpl::~SigninClientImpl() {
  signin_error_controller_->RemoveObserver(this);
}

void SigninClientImpl::Shutdown() {
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
}

void SigninClientImpl::DoFinalInit() {
}

// static
bool SigninClientImpl::AllowsSigninCookies(
    ios::ChromeBrowserState* browser_state) {
  scoped_refptr<content_settings::CookieSettings> cookie_settings =
      ios::CookieSettingsFactory::GetForBrowserState(browser_state);
  return SettingsAllowSigninCookies(cookie_settings.get());
}

// static
bool SigninClientImpl::SettingsAllowSigninCookies(
    content_settings::CookieSettings* cookie_settings) {
  GURL gaia_url = GaiaUrls::GetInstance()->gaia_url();
  GURL google_url = GaiaUrls::GetInstance()->google_url();
  return cookie_settings &&
         cookie_settings->IsSettingCookieAllowed(gaia_url, gaia_url) &&
         cookie_settings->IsSettingCookieAllowed(google_url, google_url);
}

// static
std::string SigninClientImpl::GenerateSigninScopedDeviceID(bool for_ephemeral) {
  std::string guid = base::GenerateGUID();
  return for_ephemeral ? kEphemeralUserDeviceIDPrefix + guid : guid;
}

PrefService* SigninClientImpl::GetPrefs() {
  return browser_state_->GetPrefs();
}

scoped_refptr<TokenWebData> SigninClientImpl::GetDatabase() {
  return ios::WebDataServiceFactory::GetTokenWebDataForBrowserState(
      browser_state_, ServiceAccessType::EXPLICIT_ACCESS);
}

bool SigninClientImpl::CanRevokeCredentials() {
  return true;
}

std::string SigninClientImpl::GetSigninScopedDeviceId() {
  std::string signin_scoped_device_id =
      GetPrefs()->GetString(prefs::kGoogleServicesSigninScopedDeviceId);
  if (signin_scoped_device_id.empty()) {
    // If device_id doesn't exist then generate new and save in prefs.
    signin_scoped_device_id = GenerateSigninScopedDeviceID(false);
    DCHECK(!signin_scoped_device_id.empty());
    GetPrefs()->SetString(prefs::kGoogleServicesSigninScopedDeviceId,
                          signin_scoped_device_id);
  }
  return signin_scoped_device_id;
}

void SigninClientImpl::OnSignedOut() {
  GetPrefs()->ClearPref(prefs::kGoogleServicesSigninScopedDeviceId);
  ios::BrowserStateInfoCache* cache = GetApplicationContext()
                                          ->GetChromeBrowserStateManager()
                                          ->GetBrowserStateInfoCache();
  size_t index = cache->GetIndexOfBrowserStateWithPath(
      browser_state_->GetOriginalChromeBrowserState()->GetStatePath());

  // If sign out occurs because Sync setup was in progress and the browser state
  // got deleted, then it is no longer in the cache.
  if (index == std::string::npos)
    return;

  cache->SetLocalAuthCredentialsOfBrowserStateAtIndex(index, std::string());
  cache->SetAuthInfoOfBrowserStateAtIndex(index, std::string(),
                                          base::string16());
  cache->SetBrowserStateSigninRequiredAtIndex(index, false);
}

net::URLRequestContextGetter* SigninClientImpl::GetURLRequestContext() {
  return browser_state_->GetRequestContext();
}

bool SigninClientImpl::ShouldMergeSigninCredentialsIntoCookieJar() {
  return false;
}

std::string SigninClientImpl::GetProductVersion() {
  return ios::GetChromeBrowserProvider()->GetVersionString();
}

bool SigninClientImpl::IsFirstRun() const {
  return false;
}

base::Time SigninClientImpl::GetInstallDate() {
  return base::Time::FromTimeT(
      GetApplicationContext()->GetMetricsService()->GetInstallDate());
}

bool SigninClientImpl::AreSigninCookiesAllowed() {
  return AllowsSigninCookies(browser_state_);
}

void SigninClientImpl::AddContentSettingsObserver(
    content_settings::Observer* observer) {
  browser_state_->GetHostContentSettingsMap()->AddObserver(observer);
}

void SigninClientImpl::RemoveContentSettingsObserver(
    content_settings::Observer* observer) {
  browser_state_->GetHostContentSettingsMap()->RemoveObserver(observer);
}

scoped_ptr<SigninClient::CookieChangedSubscription>
SigninClientImpl::AddCookieChangedCallback(
    const GURL& url,
    const std::string& name,
    const net::CookieStore::CookieChangedCallback& callback) {
  scoped_refptr<net::URLRequestContextGetter> context_getter =
      browser_state_->GetRequestContext();
  DCHECK(context_getter.get());
  scoped_ptr<SigninCookieChangedSubscription> subscription(
      new SigninCookieChangedSubscription(context_getter, url, name, callback));
  return subscription.Pass();
}

void SigninClientImpl::OnSignedIn(const std::string& account_id,
                                  const std::string& gaia_id,
                                  const std::string& username,
                                  const std::string& password) {
  ios::ChromeBrowserStateManager* browser_state_manager =
      GetApplicationContext()->GetChromeBrowserStateManager();
  ios::BrowserStateInfoCache* cache =
      browser_state_manager->GetBrowserStateInfoCache();
  size_t index = cache->GetIndexOfBrowserStateWithPath(
      browser_state_->GetOriginalChromeBrowserState()->GetStatePath());
  if (index != std::string::npos) {
    cache->SetAuthInfoOfBrowserStateAtIndex(index, gaia_id,
                                            base::UTF8ToUTF16(username));
  }
}

bool SigninClientImpl::UpdateAccountInfo(
    AccountTrackerService::AccountInfo* out_account_info) {
  DCHECK(!out_account_info->account_id.empty());
  ios::AccountInfo account_info =
      GetIOSProvider()->GetAccountInfo(out_account_info->account_id);
  if (account_info.gaia.empty()) {
    // There is no account information for this account, so there is nothing
    // to be updated here.
    return false;
  }

  bool updated = false;
  if (out_account_info->gaia.empty()) {
    out_account_info->gaia = account_info.gaia;
    updated = true;
  } else if (out_account_info->gaia != account_info.gaia) {
    // The GAIA id of an account never changes. Avoid updating the wrong
    // account if this occurs somehow.
    NOTREACHED() << "out_account_info->gaia = '" << out_account_info->gaia
                 << "' ; account_info.gaia = '" << account_info.gaia << "'";
    return false;
  }
  if (out_account_info->email != account_info.email) {
    out_account_info->email = account_info.email;
    updated = true;
  }
  return updated;
}

ios::ProfileOAuth2TokenServiceIOSProvider* SigninClientImpl::GetIOSProvider() {
  return ios::GetChromeBrowserProvider()
      ->GetProfileOAuth2TokenServiceIOSProvider();
}

void SigninClientImpl::OnErrorChanged() {
  ios::BrowserStateInfoCache* cache = GetApplicationContext()
                                          ->GetChromeBrowserStateManager()
                                          ->GetBrowserStateInfoCache();
  size_t index = cache->GetIndexOfBrowserStateWithPath(
      browser_state_->GetOriginalChromeBrowserState()->GetStatePath());
  if (index == std::string::npos)
    return;

  cache->SetBrowserStateIsAuthErrorAtIndex(
      index, signin_error_controller_->HasError());
}

void SigninClientImpl::OnGetTokenInfoResponse(
    scoped_ptr<base::DictionaryValue> token_info) {
  if (!token_info->HasKey("error")) {
    std::string handle;
    if (token_info->GetString("token_handle", &handle)) {
      ios::BrowserStateInfoCache* cache = GetApplicationContext()
                                              ->GetChromeBrowserStateManager()
                                              ->GetBrowserStateInfoCache();
      size_t index = cache->GetIndexOfBrowserStateWithPath(
          browser_state_->GetOriginalChromeBrowserState()->GetStatePath());
      cache->SetPasswordChangeDetectionTokenAtIndex(index, handle);
    } else {
      NOTREACHED();
    }
  }
  oauth_request_.reset();
}

void SigninClientImpl::OnOAuthError() {
  // Ignore the failure.  It's not essential and we'll try again next time.
  oauth_request_.reset();
}

void SigninClientImpl::OnNetworkError(int response_code) {
  // Ignore the failure.  It's not essential and we'll try again next time.
  oauth_request_.reset();
}

void SigninClientImpl::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const base::Time& expiration_time) {
  // Exchange the access token for a handle that can be used for later
  // verification that the token is still valid (i.e. the password has not
  // been changed).
  if (!oauth_client_) {
    oauth_client_.reset(
        new gaia::GaiaOAuthClient(browser_state_->GetRequestContext()));
  }
  oauth_client_->GetTokenInfo(access_token, 3 /* retries */, this);
}

void SigninClientImpl::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  // Ignore the failure.  It's not essential and we'll try again next time.
  oauth_request_.reset();
}

void SigninClientImpl::OnNetworkChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  if (type >= net::NetworkChangeNotifier::ConnectionType::CONNECTION_NONE)
    return;

  for (const base::Closure& callback : delayed_callbacks_)
    callback.Run();

  delayed_callbacks_.clear();
}

void SigninClientImpl::DelayNetworkCall(const base::Closure& callback) {
  // Don't bother if we don't have any kind of network connection.
  if (net::NetworkChangeNotifier::IsOffline()) {
    delayed_callbacks_.push_back(callback);
  } else {
    callback.Run();
  }
}

GaiaAuthFetcher* SigninClientImpl::CreateGaiaAuthFetcher(
    GaiaAuthConsumer* consumer,
    const std::string& source,
    net::URLRequestContextGetter* getter) {
  return new GaiaAuthFetcherIOS(consumer, source, getter, browser_state_);
}
