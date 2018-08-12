// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/ntp_snippets_service.h"

#include <algorithm>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/task_runner_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/ntp_snippets/pref_names.h"
#include "components/ntp_snippets/switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/suggestions/proto/suggestions.pb.h"

using suggestions::ChromeSuggestion;
using suggestions::SuggestionsProfile;
using suggestions::SuggestionsService;

namespace ntp_snippets {

namespace {

const int kFetchingIntervalWifiChargingSeconds = 30 * 60;
const int kFetchingIntervalWifiSeconds = 2 * 60 * 60;
const int kFetchingIntervalFallbackSeconds = 24 * 60 * 60;

const int kDefaultExpiryTimeMins = 24 * 60;

base::TimeDelta GetFetchingInterval(const char* switch_name,
                                    int default_value_seconds) {
  int value_seconds = default_value_seconds;
  const base::CommandLine& cmdline = *base::CommandLine::ForCurrentProcess();
  if (cmdline.HasSwitch(switch_name)) {
    std::string str = cmdline.GetSwitchValueASCII(switch_name);
    int switch_value_seconds = 0;
    if (base::StringToInt(str, &switch_value_seconds))
      value_seconds = switch_value_seconds;
    else
      LOG(WARNING) << "Invalid value for switch " << switch_name;
  }
  return base::TimeDelta::FromSeconds(value_seconds);
}

base::TimeDelta GetFetchingIntervalWifiCharging() {
  return GetFetchingInterval(switches::kFetchingIntervalWifiChargingSeconds,
                             kFetchingIntervalWifiChargingSeconds);
}

base::TimeDelta GetFetchingIntervalWifi() {
  return GetFetchingInterval(switches::kFetchingIntervalWifiSeconds,
                             kFetchingIntervalWifiSeconds);
}

base::TimeDelta GetFetchingIntervalFallback() {
  return GetFetchingInterval(switches::kFetchingIntervalFallbackSeconds,
                             kFetchingIntervalFallbackSeconds);
}

// Extracts the hosts from |suggestions| and returns them in a set.
std::set<std::string> GetSuggestionsHosts(
    const SuggestionsProfile& suggestions) {
  std::set<std::string> hosts;
  for (int i = 0; i < suggestions.suggestions_size(); ++i) {
    const ChromeSuggestion& suggestion = suggestions.suggestions(i);
    GURL url(suggestion.url());
    if (url.is_valid())
      hosts.insert(url.host());
  }
  return hosts;
}

const char kContentInfo[] = "contentInfo";

// Parses snippets from |list| and adds them to |snippets|. Returns true on
// success, false if anything went wrong.
bool AddSnippetsFromListValue(const base::ListValue& list,
                              NTPSnippetsService::NTPSnippetStorage* snippets) {
  for (const base::Value* const value : list) {
    const base::DictionaryValue* dict = nullptr;
    if (!value->GetAsDictionary(&dict))
      return false;

    const base::DictionaryValue* content = nullptr;
    if (!dict->GetDictionary(kContentInfo, &content))
      return false;
    scoped_ptr<NTPSnippet> snippet = NTPSnippet::CreateFromDictionary(*content);
    if (!snippet)
      return false;

    snippets->push_back(std::move(snippet));
  }
  return true;
}

scoped_ptr<base::ListValue> SnippetsToListValue(
    const NTPSnippetsService::NTPSnippetStorage& snippets) {
  scoped_ptr<base::ListValue> list(new base::ListValue);
  for (const auto& snippet : snippets) {
    scoped_ptr<base::DictionaryValue> dict(new base::DictionaryValue);
    dict->Set(kContentInfo, snippet->ToDictionary());
    list->Append(std::move(dict));
  }
  return list;
}

}  // namespace

NTPSnippetsService::NTPSnippetsService(
    PrefService* pref_service,
    SuggestionsService* suggestions_service,
    scoped_refptr<base::SequencedTaskRunner> file_task_runner,
    const std::string& application_language_code,
    NTPSnippetsScheduler* scheduler,
    scoped_ptr<NTPSnippetsFetcher> snippets_fetcher,
    const ParseJSONCallback& parse_json_callback)
    : pref_service_(pref_service),
      suggestions_service_(suggestions_service),
      file_task_runner_(file_task_runner),
      application_language_code_(application_language_code),
      scheduler_(scheduler),
      snippets_fetcher_(std::move(snippets_fetcher)),
      parse_json_callback_(parse_json_callback),
      weak_ptr_factory_(this) {
  snippets_fetcher_subscription_ = snippets_fetcher_->AddCallback(base::Bind(
      &NTPSnippetsService::OnSnippetsDownloaded, base::Unretained(this)));
}

NTPSnippetsService::~NTPSnippetsService() {}

// static
void NTPSnippetsService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(prefs::kSnippets);
  registry->RegisterListPref(prefs::kDiscardedSnippets);
  registry->RegisterListPref(prefs::kSnippetHosts);
}

void NTPSnippetsService::Init(bool enabled) {
  if (enabled) {
    // |suggestions_service_| can be null in tests.
    if (suggestions_service_) {
      suggestions_service_subscription_ = suggestions_service_->AddCallback(
          base::Bind(&NTPSnippetsService::OnSuggestionsChanged,
                     base::Unretained(this)));
    }

    // Get any existing snippets immediately from prefs.
    LoadDiscardedSnippetsFromPrefs();
    LoadSnippetsFromPrefs();

    // If we don't have any snippets yet, start a fetch.
    if (snippets_.empty())
      FetchSnippets();
  }

  // The scheduler only exists on Android so far, it's null on other platforms.
  if (!scheduler_)
    return;

  if (enabled) {
    scheduler_->Schedule(GetFetchingIntervalWifiCharging(),
                         GetFetchingIntervalWifi(),
                         GetFetchingIntervalFallback());
  } else {
    scheduler_->Unschedule();
  }
}

void NTPSnippetsService::Shutdown() {
  FOR_EACH_OBSERVER(NTPSnippetsServiceObserver, observers_,
                    NTPSnippetsServiceShutdown());
}

void NTPSnippetsService::FetchSnippets() {
  // |suggestions_service_| can be null in tests.
  if (!suggestions_service_)
    return;

  FetchSnippetsImpl(GetSuggestionsHosts(
      suggestions_service_->GetSuggestionsDataFromCache()));
}

bool NTPSnippetsService::DiscardSnippet(const GURL& url) {
  auto it = std::find_if(snippets_.begin(), snippets_.end(),
                         [&url](const scoped_ptr<NTPSnippet>& snippet) {
                           return snippet->url() == url;
                         });
  if (it == snippets_.end())
    return false;
  discarded_snippets_.push_back(std::move(*it));
  snippets_.erase(it);
  StoreDiscardedSnippetsToPrefs();
  StoreSnippetsToPrefs();
  return true;
}

void NTPSnippetsService::AddObserver(NTPSnippetsServiceObserver* observer) {
  observers_.AddObserver(observer);
  observer->NTPSnippetsServiceLoaded();
}

void NTPSnippetsService::RemoveObserver(NTPSnippetsServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

void NTPSnippetsService::OnSuggestionsChanged(
    const SuggestionsProfile& suggestions) {
  std::set<std::string> hosts = GetSuggestionsHosts(suggestions);
  if (hosts == GetSnippetHostsFromPrefs())
    return;

  // Remove existing snippets that aren't in the suggestions anymore.
  snippets_.erase(
      std::remove_if(snippets_.begin(), snippets_.end(),
                     [&hosts](const scoped_ptr<NTPSnippet>& snippet) {
                       return !hosts.count(snippet->url().host());
                     }),
      snippets_.end());

  StoreSnippetsToPrefs();
  StoreSnippetHostsToPrefs(hosts);

  FOR_EACH_OBSERVER(NTPSnippetsServiceObserver, observers_,
                    NTPSnippetsServiceLoaded());

  FetchSnippetsImpl(hosts);
}

void NTPSnippetsService::OnSnippetsDownloaded(
    const std::string& snippets_json) {
  parse_json_callback_.Run(
      snippets_json, base::Bind(&NTPSnippetsService::OnJsonParsed,
                                weak_ptr_factory_.GetWeakPtr(), snippets_json),
      base::Bind(&NTPSnippetsService::OnJsonError,
                 weak_ptr_factory_.GetWeakPtr(), snippets_json));
}

void NTPSnippetsService::OnJsonParsed(const std::string& snippets_json,
                                      scoped_ptr<base::Value> parsed) {
  LOG_IF(WARNING, !LoadFromValue(*parsed)) << "Received invalid snippets: "
                                           << snippets_json;
}

void NTPSnippetsService::OnJsonError(const std::string& snippets_json,
                                     const std::string& error) {
  LOG(WARNING) << "Received invalid JSON (" << error << "): " << snippets_json;
}

void NTPSnippetsService::FetchSnippetsImpl(
    const std::set<std::string>& hosts) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDontRestrict)) {
    snippets_fetcher_->FetchSnippets(std::set<std::string>());
    return;
  }
  if (!hosts.empty())
    snippets_fetcher_->FetchSnippets(hosts);
}

bool NTPSnippetsService::LoadFromValue(const base::Value& value) {
  const base::DictionaryValue* top_dict = nullptr;
  if (!value.GetAsDictionary(&top_dict))
    return false;

  const base::ListValue* list = nullptr;
  if (!top_dict->GetList("recos", &list))
    return false;

  return LoadFromListValue(*list);
}

bool NTPSnippetsService::LoadFromListValue(const base::ListValue& list) {
  NTPSnippetStorage new_snippets;
  if (!AddSnippetsFromListValue(list, &new_snippets))
    return false;
  for (scoped_ptr<NTPSnippet>& snippet : new_snippets) {
    // If this snippet has previously been discarded, don't add it again.
    if (HasDiscardedSnippet(snippet->url()))
      continue;

    // If the snippet has no publish/expiry dates, fill in defaults.
    if (snippet->publish_date().is_null())
      snippet->set_publish_date(base::Time::Now());
    if (snippet->expiry_date().is_null()) {
      snippet->set_expiry_date(
          snippet->publish_date() +
          base::TimeDelta::FromMinutes(kDefaultExpiryTimeMins));
    }

    // Check if we already have a snippet with the same URL. If so, replace it
    // rather than adding a duplicate.
    const GURL& url = snippet->url();
    auto it = std::find_if(snippets_.begin(), snippets_.end(),
                           [&url](const scoped_ptr<NTPSnippet>& old_snippet) {
                             return old_snippet->url() == url;
                           });
    if (it != snippets_.end())
      *it = std::move(snippet);
    else
      snippets_.push_back(std::move(snippet));
  }

  // Immediately remove any already-expired snippets. This will also notify our
  // observers and schedule the expiry timer.
  RemoveExpiredSnippets();

  return true;
}

void NTPSnippetsService::LoadSnippetsFromPrefs() {
  bool success = LoadFromListValue(*pref_service_->GetList(prefs::kSnippets));
  DCHECK(success) << "Failed to parse snippets from prefs";
}

void NTPSnippetsService::StoreSnippetsToPrefs() {
  pref_service_->Set(prefs::kSnippets, *SnippetsToListValue(snippets_));
}

void NTPSnippetsService::LoadDiscardedSnippetsFromPrefs() {
  discarded_snippets_.clear();
  bool success = AddSnippetsFromListValue(
      *pref_service_->GetList(prefs::kDiscardedSnippets),
      &discarded_snippets_);
  DCHECK(success) << "Failed to parse discarded snippets from prefs";
}

void NTPSnippetsService::StoreDiscardedSnippetsToPrefs() {
  pref_service_->Set(prefs::kDiscardedSnippets,
                     *SnippetsToListValue(discarded_snippets_));
}

std::set<std::string> NTPSnippetsService::GetSnippetHostsFromPrefs() const {
  std::set<std::string> hosts;
  const base::ListValue* list = pref_service_->GetList(prefs::kSnippetHosts);
  for (const base::Value* value : *list) {
    std::string str;
    bool success = value->GetAsString(&str);
    DCHECK(success) << "Failed to parse snippet host from prefs";
    hosts.insert(std::move(str));
  }
  return hosts;
}

void NTPSnippetsService::StoreSnippetHostsToPrefs(
    const std::set<std::string>& hosts) {
  base::ListValue list;
  for (const std::string& host : hosts)
    list.AppendString(host);
  pref_service_->Set(prefs::kSnippetHosts, list);
}

bool NTPSnippetsService::HasDiscardedSnippet(const GURL& url) const {
  auto it = std::find_if(discarded_snippets_.begin(), discarded_snippets_.end(),
                         [&url](const scoped_ptr<NTPSnippet>& snippet) {
                           return snippet->url() == url;
                         });
  return it != discarded_snippets_.end();
}

void NTPSnippetsService::RemoveExpiredSnippets() {
  base::Time expiry = base::Time::Now();

  snippets_.erase(
      std::remove_if(snippets_.begin(), snippets_.end(),
                     [&expiry](const scoped_ptr<NTPSnippet>& snippet) {
                       return snippet->expiry_date() <= expiry;
                     }),
      snippets_.end());
  StoreSnippetsToPrefs();

  discarded_snippets_.erase(
      std::remove_if(discarded_snippets_.begin(), discarded_snippets_.end(),
                     [&expiry](const scoped_ptr<NTPSnippet>& snippet) {
                       return snippet->expiry_date() <= expiry;
                     }),
                     discarded_snippets_.end());
  StoreDiscardedSnippetsToPrefs();

  FOR_EACH_OBSERVER(NTPSnippetsServiceObserver, observers_,
                    NTPSnippetsServiceLoaded());

  // If there are any snippets left, schedule a timer for the next expiry.
  if (snippets_.empty() && discarded_snippets_.empty())
    return;

  base::Time next_expiry = base::Time::Max();
  for (const auto& snippet : snippets_) {
    if (snippet->expiry_date() < next_expiry)
      next_expiry = snippet->expiry_date();
  }
  for (const auto& snippet : discarded_snippets_) {
    if (snippet->expiry_date() < next_expiry)
      next_expiry = snippet->expiry_date();
  }
  DCHECK_GT(next_expiry, expiry);
  expiry_timer_.Start(FROM_HERE, next_expiry - expiry,
                      base::Bind(&NTPSnippetsService::RemoveExpiredSnippets,
                                 base::Unretained(this)));
}

}  // namespace ntp_snippets
