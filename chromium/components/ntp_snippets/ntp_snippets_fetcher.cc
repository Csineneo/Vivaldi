// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/ntp_snippets_fetcher.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task_runner_util.h"
#include "google_apis/google_api_keys.h"
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

const char kContentSnippetsServerFormat[] =
    "https://chromereader-pa.googleapis.com/v1/fetch?key=%s";

const char kRequestParameterFormat[] =
    "{"
    "  \"response_detail_level\": \"STANDARD\","
    "  \"advanced_options\": {"
    "    \"local_scoring_params\": {"
    "      \"content_params\": {"
    "        \"only_return_personalized_results\": false"
    "      },"
    "      \"content_restricts\": {"
    "        \"type\": \"METADATA\","
    "        \"value\": \"TITLE\""
    "      },"
    "      \"content_restricts\": {"
    "        \"type\": \"METADATA\","
    "        \"value\": \"SNIPPET\""
    "      },"
    "      \"content_restricts\": {"
    "        \"type\": \"METADATA\","
    "        \"value\": \"THUMBNAIL\""
    "      }"
    "%s"
    "    },"
    "    \"global_scoring_params\": {"
    "      \"num_to_return\": 10"
    "    }"
    "  }"
    "}";

const char kHostRestrictFormat[] =
    "      ,\"content_selectors\": {"
    "        \"type\": \"HOST_RESTRICT\","
    "        \"value\": \"%s\""
    "      }";

NTPSnippetsFetcher::NTPSnippetsFetcher(
    scoped_refptr<base::SequencedTaskRunner> file_task_runner,
    scoped_refptr<URLRequestContextGetter> url_request_context_getter,
    bool is_stable_channel)
    : file_task_runner_(file_task_runner),
      url_request_context_getter_(url_request_context_getter),
      is_stable_channel_(is_stable_channel) {}

NTPSnippetsFetcher::~NTPSnippetsFetcher() {
}

scoped_ptr<NTPSnippetsFetcher::SnippetsAvailableCallbackList::Subscription>
NTPSnippetsFetcher::AddCallback(const SnippetsAvailableCallback& callback) {
  return callback_list_.Add(callback);
}

void NTPSnippetsFetcher::FetchSnippets(const std::set<std::string>& hosts) {
  // TODO(treib): What to do if there's already a pending request?
  const std::string& key = is_stable_channel_
                               ? google_apis::GetAPIKey()
                               : google_apis::GetNonStableAPIKey();
  std::string url =
      base::StringPrintf(kContentSnippetsServerFormat, key.c_str());
  url_fetcher_ = URLFetcher::Create(GURL(url), URLFetcher::POST, this);
  url_fetcher_->SetRequestContext(url_request_context_getter_.get());
  url_fetcher_->SetLoadFlags(net::LOAD_DO_NOT_SEND_COOKIES |
                             net::LOAD_DO_NOT_SAVE_COOKIES);
  HttpRequestHeaders headers;
  headers.SetHeader("Content-Type", "application/json; charset=UTF-8");
  url_fetcher_->SetExtraRequestHeaders(headers.ToString());
  std::string host_restricts;
  for (const std::string& host : hosts)
    host_restricts += base::StringPrintf(kHostRestrictFormat, host.c_str());
  url_fetcher_->SetUploadData("application/json",
                              base::StringPrintf(kRequestParameterFormat,
                                                 host_restricts.c_str()));

  // Fetchers are sometimes cancelled because a network change was detected.
  url_fetcher_->SetAutomaticallyRetryOnNetworkChanges(3);
  // Try to make fetching the files bit more robust even with poor connection.
  url_fetcher_->SetMaxRetriesOn5xx(3);
  url_fetcher_->Start();
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

  std::string response;
  bool stores_result_to_string = source->GetResponseAsString(&response);
  DCHECK(stores_result_to_string);

  callback_list_.Notify(response);
}

}  // namespace ntp_snippets
