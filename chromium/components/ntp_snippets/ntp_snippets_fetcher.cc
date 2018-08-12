// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/ntp_snippets_fetcher.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/task_runner_util.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/browser/signin_tracker.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_fetcher.h"

using net::URLFetcher;
using net::URLRequestContextGetter;
using net::HttpRequestHeaders;
using net::URLRequestStatus;

namespace ntp_snippets {

const char kSnippetSuggestionsFilename[] = "ntp_snippets.json";
const char kApiScope[] = "https://www.googleapis.com/auth/webhistory";
const char kContentSnippetsServer[] =
    "https://chromereader-pa.googleapis.com/v1/fetch";
const char kAuthorizationRequestHeaderFormat[] = "Bearer %s";

const char kUnpersonalizedRequestParameters[] =
    "{ \"response_detail_level\": \"FULL_DEBUG\", \"advanced_options\": { "
    "\"local_scoring_params\": {\"content_params\" : { "
    "\"only_return_personalized_results\": false } }, "
    "\"global_scoring_params\": { \"num_to_return\": 10 } } }";

base::FilePath GetSnippetsSuggestionsPath(const base::FilePath& base_dir) {
  return base_dir.AppendASCII(kSnippetSuggestionsFilename);
}

NTPSnippetsFetcher::NTPSnippetsFetcher(
    scoped_refptr<base::SequencedTaskRunner> file_task_runner,
    SigninManagerBase* signin_manager,
    OAuth2TokenService* token_service,
    scoped_refptr<URLRequestContextGetter> url_request_context_getter,
    const base::FilePath& base_download_path)
    : OAuth2TokenService::Consumer("NTP_snippets"),
      file_task_runner_(file_task_runner),
      url_request_context_getter_(url_request_context_getter),
      signin_manager_(signin_manager),
      token_service_(token_service),
      download_path_(GetSnippetsSuggestionsPath(base_download_path)),
      waiting_for_refresh_token_(false),
      weak_ptr_factory_(this) {}

NTPSnippetsFetcher::~NTPSnippetsFetcher() {
  if (waiting_for_refresh_token_)
    token_service_->RemoveObserver(this);
}

scoped_ptr<NTPSnippetsFetcher::SnippetsAvailableCallbackList::Subscription>
NTPSnippetsFetcher::AddCallback(const SnippetsAvailableCallback& callback) {
  return callback_list_.Add(callback);
}

void NTPSnippetsFetcher::FetchSnippets(bool overwrite) {
  if (overwrite) {
    StartFetch();
  } else {
    base::PostTaskAndReplyWithResult(
        file_task_runner_.get(), FROM_HERE,
        base::Bind(&base::PathExists, download_path_),
        base::Bind(&NTPSnippetsFetcher::OnFileExistsCheckDone,
                   weak_ptr_factory_.GetWeakPtr()));
  }
}

void NTPSnippetsFetcher::OnFileExistsCheckDone(bool exists) {
  if (exists) {
    NotifyObservers();
  } else {
    StartFetch();
  }
}

void NTPSnippetsFetcher::StartFetch() {
  if (signin_manager_->IsAuthenticated()) {
    StartTokenRequest();
  } else {
    if (!waiting_for_refresh_token_) {
      // Wait until we get a refresh token.
      waiting_for_refresh_token_ = true;
      token_service_->AddObserver(this);
    }
  }
}

void NTPSnippetsFetcher::StartTokenRequest() {
  OAuth2TokenService::ScopeSet scopes;
  scopes.insert(kApiScope);
  oauth_request_ = token_service_->StartRequest(
      signin_manager_->GetAuthenticatedAccountId(), scopes, this);
}

void NTPSnippetsFetcher::NotifyObservers() {
  callback_list_.Notify(download_path_);
}

////////////////////////////////////////////////////////////////////////////////
// OAuth2TokenService::Consumer overrides
void NTPSnippetsFetcher::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const base::Time& expiration_time) {
  oauth_request_.reset();
  url_fetcher_ =
      URLFetcher::Create(GURL(kContentSnippetsServer), URLFetcher::POST, this);
  url_fetcher_->SetRequestContext(url_request_context_getter_.get());
  url_fetcher_->SetLoadFlags(net::LOAD_DO_NOT_SEND_COOKIES |
                             net::LOAD_DO_NOT_SAVE_COOKIES);
  HttpRequestHeaders headers;
  headers.SetHeader("Authorization",
                    base::StringPrintf(kAuthorizationRequestHeaderFormat,
                                       access_token.c_str()));
  headers.SetHeader("Content-Type", "application/json; charset=UTF-8");
  url_fetcher_->SetExtraRequestHeaders(headers.ToString());
  url_fetcher_->SetUploadData("application/json",
                              kUnpersonalizedRequestParameters);
  url_fetcher_->SaveResponseToTemporaryFile(file_task_runner_.get());
  url_fetcher_->Start();
}

void NTPSnippetsFetcher::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  oauth_request_.reset();
  DLOG(ERROR) << "Unable to get token: " << error.ToString();
}

////////////////////////////////////////////////////////////////////////////////
// OAuth2TokenService::Observer overrides
void NTPSnippetsFetcher::OnRefreshTokenAvailable(
    const std::string& account_id) {
  token_service_->RemoveObserver(this);
  waiting_for_refresh_token_ = false;
  StartTokenRequest();
}

////////////////////////////////////////////////////////////////////////////////
// URLFetcherDelegate overrides
void NTPSnippetsFetcher::OnURLFetchComplete(const URLFetcher* source) {
  DCHECK_EQ(url_fetcher_.get(), source);

  const URLRequestStatus& status = source->GetStatus();
  if (!status.is_success()) {
    DLOG(WARNING) << "URLRequestStatus error " << status.error()
                  << " while trying to download " << source->GetURL().spec();
    return;
  }

  int response_code = source->GetResponseCode();
  if (response_code != net::HTTP_OK) {
    DLOG(WARNING) << "HTTP error " << response_code
                  << " while trying to download " << source->GetURL().spec();
    return;
  }

  base::FilePath response_path;
  source->GetResponseAsFilePath(false, &response_path);

  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(), FROM_HERE,
      base::Bind(&base::Move, response_path, download_path_),
      base::Bind(&NTPSnippetsFetcher::OnFileMoveDone,
                 weak_ptr_factory_.GetWeakPtr()));
}

void NTPSnippetsFetcher::OnFileMoveDone(bool success) {
  if (!success) {
    DLOG(WARNING) << "Could not move file to "
                  << download_path_.LossyDisplayName();
    return;
  }

  NotifyObservers();
}

}  // namespace ntp_snippets
