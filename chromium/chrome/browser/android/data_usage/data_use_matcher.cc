// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/data_usage/data_use_matcher.h"

#include <stddef.h>
#include <stdint.h>

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "chrome/browser/android/data_usage/external_data_use_observer_bridge.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"

namespace chrome {

namespace android {

DataUseMatcher::DataUseMatcher(
    const base::WeakPtr<DataUseTabModel>& data_use_tab_model,
    const ExternalDataUseObserverBridge* external_data_use_observer_bridge,
    const base::TimeDelta& default_matching_rule_expiration_duration)
    : data_use_tab_model_(data_use_tab_model),
      default_matching_rule_expiration_duration_(
          default_matching_rule_expiration_duration),
      tick_clock_(new base::DefaultTickClock()),
      external_data_use_observer_bridge_(external_data_use_observer_bridge) {
  DCHECK(external_data_use_observer_bridge_);
}

DataUseMatcher::~DataUseMatcher() {}

void DataUseMatcher::RegisterURLRegexes(
    const std::vector<std::string>& app_package_names,
    const std::vector<std::string>& domain_path_regexes,
    const std::vector<std::string>& labels) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(app_package_names.size(), domain_path_regexes.size());
  DCHECK_EQ(app_package_names.size(), labels.size());

  base::hash_set<std::string> removed_matching_rule_labels;
  uint32_t invalid_rules = 0;

  for (const auto& matching_rule : matching_rules_)
    removed_matching_rule_labels.insert(matching_rule->label());

  matching_rules_.clear();
  re2::RE2::Options options(re2::RE2::DefaultOptions);
  options.set_case_sensitive(false);

  for (size_t i = 0; i < domain_path_regexes.size(); ++i) {
    const std::string& url_regex = domain_path_regexes.at(i);
    std::string app_package_name;
    base::TimeTicks expiration;
    const base::TimeTicks now_ticks = tick_clock_->NowTicks();

    ParsePackageField(app_package_names.at(i), &app_package_name, &expiration);
    if (url_regex.empty() && app_package_name.empty()) {
      invalid_rules++;
      continue;
    }
    std::unique_ptr<re2::RE2> pattern(new re2::RE2(url_regex, options));
    if (!pattern->ok()) {
      invalid_rules++;
      continue;
    }

    if (expiration <= now_ticks)
      continue;  // skip expired matching rules.
    DCHECK(!labels.at(i).empty());
    matching_rules_.push_back(base::WrapUnique(new MatchingRule(
        app_package_name, std::move(pattern), labels.at(i), expiration)));

    removed_matching_rule_labels.erase(labels.at(i));
  }

  for (const std::string& label : removed_matching_rule_labels) {
    if (data_use_tab_model_)
      data_use_tab_model_->OnTrackingLabelRemoved(label);
  }
  UMA_HISTOGRAM_COUNTS_100("DataUsage.MatchingRulesCount.Valid",
                           matching_rules_.size());
  UMA_HISTOGRAM_COUNTS_100("DataUsage.MatchingRulesCount.Invalid",
                           invalid_rules);

  external_data_use_observer_bridge_->ShouldRegisterAsDataUseObserver(
      !matching_rules_.empty());
}

bool DataUseMatcher::MatchesURL(const GURL& url, std::string* label) const {
  const base::TimeTicks now_ticks = tick_clock_->NowTicks();
  DCHECK(thread_checker_.CalledOnValidThread());
  *label = "";

  if (!url.is_valid() || url.is_empty())
    return false;

  for (const auto& matching_rule : matching_rules_) {
    if (matching_rule->expiration() <= now_ticks)
      continue;  // skip expired matching rules.
    base::TimeTicks begin = base::TimeTicks::Now();
    bool match = re2::RE2::FullMatch(url.spec(), *(matching_rule->pattern()));
    UMA_HISTOGRAM_TIMES("DataUsage.Perf.URLRegexMatchDuration",
                        base::TimeTicks::Now() - begin);
    if (match) {
      *label = matching_rule->label();
      return true;
    }
  }

  return false;
}

bool DataUseMatcher::MatchesAppPackageName(const std::string& app_package_name,
                                           std::string* label) const {
  const base::TimeTicks now_ticks = tick_clock_->NowTicks();
  DCHECK(thread_checker_.CalledOnValidThread());
  *label = "";

  if (app_package_name.empty())
    return false;

  for (const auto& matching_rule : matching_rules_) {
    if (matching_rule->expiration() <= now_ticks)
      continue;  // skip expired matching rules.
    if (app_package_name == matching_rule->app_package_name()) {
      *label = matching_rule->label();
      return true;
    }
  }

  return false;
}

void DataUseMatcher::FetchMatchingRules() {
  DCHECK(thread_checker_.CalledOnValidThread());
  external_data_use_observer_bridge_->FetchMatchingRules();
}

bool DataUseMatcher::HasValidRules() const {
  return !matching_rules_.empty();
}

void DataUseMatcher::ParsePackageField(const std::string& app_package_name,
                                       std::string* new_app_package_name,
                                       base::TimeTicks* expiration) const {
  const char separator = '|';
  size_t index = app_package_name.find_last_of(separator);
  uint64_t
      expiration_milliseconds;  // expiration time as milliSeconds since epoch.
  if (index != std::string::npos &&
      base::StringToUint64(app_package_name.substr(index + 1),
                           &expiration_milliseconds)) {
    *new_app_package_name = app_package_name.substr(0, index);
    *expiration = base::TimeTicks::UnixEpoch() +
                  base::TimeDelta::FromMilliseconds(expiration_milliseconds);
    return;
  }
  *expiration =
      tick_clock_->NowTicks() + default_matching_rule_expiration_duration_;
  *new_app_package_name = app_package_name;
}

DataUseMatcher::MatchingRule::MatchingRule(const std::string& app_package_name,
                                           std::unique_ptr<re2::RE2> pattern,
                                           const std::string& label,
                                           const base::TimeTicks& expiration)
    : app_package_name_(app_package_name),
      pattern_(std::move(pattern)),
      label_(label),
      expiration_(expiration) {}

DataUseMatcher::MatchingRule::~MatchingRule() {}

const re2::RE2* DataUseMatcher::MatchingRule::pattern() const {
  return pattern_.get();
}

const std::string& DataUseMatcher::MatchingRule::app_package_name() const {
  return app_package_name_;
}

const std::string& DataUseMatcher::MatchingRule::label() const {
  return label_;
}

const base::TimeTicks& DataUseMatcher::MatchingRule::expiration() const {
  return expiration_;
}

}  // namespace android

}  // namespace chrome
