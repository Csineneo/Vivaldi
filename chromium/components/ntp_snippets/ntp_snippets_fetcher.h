// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NTP_SNIPPETS_NTP_SNIPPETS_FETCHER_H_
#define COMPONENTS_NTP_SNIPPETS_NTP_SNIPPETS_FETCHER_H_

#include <set>
#include <string>

#include "base/callback.h"
#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_context_getter.h"

namespace ntp_snippets {

// Fetches snippet data for the NTP from the server
class NTPSnippetsFetcher : public net::URLFetcherDelegate {
 public:
  using SnippetsAvailableCallback = base::Callback<void(const std::string&)>;
  using SnippetsAvailableCallbackList =
      base::CallbackList<void(const std::string&)>;

  NTPSnippetsFetcher(
      scoped_refptr<base::SequencedTaskRunner> file_task_runner,
      scoped_refptr<net::URLRequestContextGetter> url_request_context_getter,
      bool is_stable_channel);
  ~NTPSnippetsFetcher() override;

  // Adds a callback that is called when a new set of snippets are downloaded.
  scoped_ptr<SnippetsAvailableCallbackList::Subscription> AddCallback(
      const SnippetsAvailableCallback& callback) WARN_UNUSED_RESULT;

  // Fetches snippets from the server. |hosts| can be used to restrict the
  // results to a set of hosts, e.g. "www.google.com". If it is empty, no
  // restrictions are applied.
  void FetchSnippets(const std::set<std::string>& hosts);

 private:
  // URLFetcherDelegate implementation.
  void OnURLFetchComplete(const net::URLFetcher* source) override;

  // The SequencedTaskRunner on which file system operations will be run.
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;

  // Holds the URL request context.
  scoped_refptr<net::URLRequestContextGetter> url_request_context_getter_;

  // The fetcher for downloading the snippets.
  scoped_ptr<net::URLFetcher> url_fetcher_;

  // The callbacks to notify when new snippets get fetched.
  SnippetsAvailableCallbackList callback_list_;

  // Flag for picking the right (stable/non-stable) API key for Chrome Reader
  bool is_stable_channel_;

  DISALLOW_COPY_AND_ASSIGN(NTPSnippetsFetcher);
};
}  // namespace ntp_snippets

#endif  // COMPONENTS_NTP_SNIPPETS_NTP_SNIPPETS_FETCHER_H_
