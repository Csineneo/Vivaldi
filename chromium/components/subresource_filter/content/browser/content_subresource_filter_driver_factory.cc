// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/content_subresource_filter_driver_factory.h"

#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/time/time.h"
#include "components/subresource_filter/content/browser/content_activation_list_utils.h"
#include "components/subresource_filter/content/browser/subresource_filter_client.h"
#include "components/subresource_filter/content/common/subresource_filter_messages.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/common/activation_list.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "components/subresource_filter/core/common/time_measurements.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ipc/ipc_message_macros.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

namespace subresource_filter {

namespace {

const char kWebContentsUserDataKey[] =
    "web_contents_subresource_filter_driver_factory";

std::string DistillURLToHostAndPath(const GURL& url) {
  return url.host() + url.path();
}

// Returns true with a probability given by |performance_measurement_rate| if
// ThreadTicks is supported, otherwise returns false.
bool ShouldMeasurePerformanceForPageLoad(double performance_measurement_rate) {
  if (!base::ThreadTicks::IsSupported())
    return false;
  return performance_measurement_rate == 1 ||
         (performance_measurement_rate > 0 &&
          base::RandDouble() < performance_measurement_rate);
}

// Records histograms about the length of redirect chains, and about the pattern
// of whether each URL in the chain matched the activation list.
#define REPORT_REDIRECT_PATTERN_FOR_SUFFIX(suffix, hits_pattern, chain_size)   \
  do {                                                                         \
    UMA_HISTOGRAM_ENUMERATION(                                                 \
        "SubresourceFilter.PageLoad.RedirectChainMatchPattern." suffix,        \
        hits_pattern, 0x10);                                                   \
    UMA_HISTOGRAM_COUNTS(                                                      \
        "SubresourceFilter.PageLoad.RedirectChainLength." suffix, chain_size); \
  } while (0)

}  // namespace

// static
void ContentSubresourceFilterDriverFactory::CreateForWebContents(
    content::WebContents* web_contents,
    std::unique_ptr<SubresourceFilterClient> client) {
  if (FromWebContents(web_contents))
    return;
  web_contents->SetUserData(kWebContentsUserDataKey,
                            new ContentSubresourceFilterDriverFactory(
                                web_contents, std::move(client)));
}

// static
ContentSubresourceFilterDriverFactory*
ContentSubresourceFilterDriverFactory::FromWebContents(
    content::WebContents* web_contents) {
  return static_cast<ContentSubresourceFilterDriverFactory*>(
      web_contents->GetUserData(kWebContentsUserDataKey));
}

// static
bool ContentSubresourceFilterDriverFactory::NavigationIsPageReload(
    const GURL& url,
    const content::Referrer& referrer,
    ui::PageTransition transition) {
  return ui::PageTransitionCoreTypeIs(transition, ui::PAGE_TRANSITION_RELOAD) ||
         // Some pages 'reload' from JavaScript by navigating to themselves.
         url == referrer.url;
}

ContentSubresourceFilterDriverFactory::ContentSubresourceFilterDriverFactory(
    content::WebContents* web_contents,
    std::unique_ptr<SubresourceFilterClient> client)
    : content::WebContentsObserver(web_contents),
      configuration_(GetActiveConfiguration()),
      client_(std::move(client)),
      throttle_manager_(
          base::MakeUnique<ContentSubresourceFilterThrottleManager>(
              this,
              client_->GetRulesetDealer(),
              web_contents)),
      activation_level_(ActivationLevel::DISABLED),
      activation_decision_(ActivationDecision::UNKNOWN),
      measure_performance_(false) {}

ContentSubresourceFilterDriverFactory::
    ~ContentSubresourceFilterDriverFactory() {}

void ContentSubresourceFilterDriverFactory::OnDocumentLoadStatistics(
    const DocumentLoadStatistics& statistics) {
  // Note: Chances of overflow are negligible.
  aggregated_document_statistics_.num_loads_total += statistics.num_loads_total;
  aggregated_document_statistics_.num_loads_evaluated +=
      statistics.num_loads_evaluated;
  aggregated_document_statistics_.num_loads_matching_rules +=
      statistics.num_loads_matching_rules;
  aggregated_document_statistics_.num_loads_disallowed +=
      statistics.num_loads_disallowed;

  aggregated_document_statistics_.evaluation_total_wall_duration +=
      statistics.evaluation_total_wall_duration;
  aggregated_document_statistics_.evaluation_total_cpu_duration +=
      statistics.evaluation_total_cpu_duration;
}

bool ContentSubresourceFilterDriverFactory::IsWhitelisted(
    const GURL& url) const {
  return whitelisted_hosts_.find(url.host()) != whitelisted_hosts_.end() ||
         client_->IsWhitelistedByContentSettings(url);
}

void ContentSubresourceFilterDriverFactory::
    OnMainResourceMatchedSafeBrowsingBlacklist(
        const GURL& url,
        const std::vector<GURL>& redirect_urls,
        safe_browsing::SBThreatType threat_type,
        safe_browsing::ThreatPatternType threat_type_metadata) {
  AddActivationListMatch(
      url, GetListForThreatTypeAndMetadata(threat_type, threat_type_metadata));
}

void ContentSubresourceFilterDriverFactory::AddHostOfURLToWhitelistSet(
    const GURL& url) {
  if (url.has_host() && url.SchemeIsHTTPOrHTTPS())
    whitelisted_hosts_.insert(url.host());
}

ContentSubresourceFilterDriverFactory::ActivationDecision
ContentSubresourceFilterDriverFactory::ComputeActivationDecisionForMainFrameURL(
    const GURL& url) const {
  if (configuration_.activation_level == ActivationLevel::DISABLED)
    return ActivationDecision::ACTIVATION_DISABLED;

  if (configuration_.activation_scope == ActivationScope::NO_SITES)
    return ActivationDecision::ACTIVATION_DISABLED;

  if (!url.SchemeIsHTTPOrHTTPS())
    return ActivationDecision::UNSUPPORTED_SCHEME;
  if (IsWhitelisted(url))
    return ActivationDecision::URL_WHITELISTED;

  switch (configuration_.activation_scope) {
    case ActivationScope::ALL_SITES:
      return ActivationDecision::ACTIVATED;
    case ActivationScope::ACTIVATION_LIST: {
      // The logic to ensure only http/https URLs are activated lives in
      // AddActivationListMatch to ensure the activation list only has relevant
      // entries.
      DCHECK(url.SchemeIsHTTPOrHTTPS() ||
             !DidURLMatchActivationList(url, configuration_.activation_list));
      bool should_activate =
          DidURLMatchActivationList(url, configuration_.activation_list);
      if (configuration_.activation_list ==
          ActivationList::PHISHING_INTERSTITIAL) {
        // Handling special case, where activation on the phishing sites also
        // mean the activation on the sites with social engineering metadata.
        should_activate |= DidURLMatchActivationList(
            url, ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL);
      }
      return should_activate ? ActivationDecision::ACTIVATED
                             : ActivationDecision::ACTIVATION_LIST_NOT_MATCHED;
    }
    default:
      return ActivationDecision::ACTIVATION_DISABLED;
  }
}

void ContentSubresourceFilterDriverFactory::OnReloadRequested() {
  UMA_HISTOGRAM_BOOLEAN("SubresourceFilter.Prompt.NumReloads", true);
  const GURL& whitelist_url = web_contents()->GetLastCommittedURL();

  // Only whitelist via content settings when using the experimental UI,
  // otherwise could get into a situation where content settings cannot be
  // adjusted.
  if (base::FeatureList::IsEnabled(
          subresource_filter::kSafeBrowsingSubresourceFilterExperimentalUI)) {
    client_->WhitelistByContentSettings(whitelist_url);
  } else {
    AddHostOfURLToWhitelistSet(whitelist_url);
  }
  web_contents()->GetController().Reload(content::ReloadType::NORMAL, true);
}

void ContentSubresourceFilterDriverFactory::WillProcessResponse(
    content::NavigationHandle* navigation_handle) {
  DCHECK(!navigation_handle->IsSameDocument());
  if (!navigation_handle->IsInMainFrame() ||
      navigation_handle->GetNetErrorCode() != net::OK) {
    return;
  }

  const GURL& url = navigation_handle->GetURL();
  const content::Referrer& referrer = navigation_handle->GetReferrer();
  ui::PageTransition transition = navigation_handle->GetPageTransition();

  RecordRedirectChainMatchPattern();

  if (configuration_.should_whitelist_site_on_reload &&
      NavigationIsPageReload(url, referrer, transition)) {
    // Whitelist this host for the current as well as subsequent navigations.
    AddHostOfURLToWhitelistSet(url);
  }

  activation_decision_ = ComputeActivationDecisionForMainFrameURL(url);
  DCHECK(activation_decision_ != ActivationDecision::UNKNOWN);
  if (activation_decision_ != ActivationDecision::ACTIVATED) {
    ResetActivationState();
    return;
  }

  activation_level_ = configuration_.activation_level;
  measure_performance_ = activation_level_ != ActivationLevel::DISABLED &&
                         ShouldMeasurePerformanceForPageLoad(
                             configuration_.performance_measurement_rate);
  ActivationState state = ActivationState(activation_level_);
  state.measure_performance = measure_performance_;
  throttle_manager_->NotifyPageActivationComputed(navigation_handle, state);
}

void ContentSubresourceFilterDriverFactory::OnFirstSubresourceLoadDisallowed() {
  if (configuration_.should_suppress_notifications)
    return;

  client_->ToggleNotificationVisibility(activation_level_ ==
                                        ActivationLevel::ENABLED);
}

bool ContentSubresourceFilterDriverFactory::ShouldSuppressActivation(
    content::NavigationHandle* navigation_handle) {
  // Never suppress subframe navigations.
  return navigation_handle->IsInMainFrame() &&
         IsWhitelisted(navigation_handle->GetURL());
}

void ContentSubresourceFilterDriverFactory::ResetActivationState() {
  navigation_chain_.clear();
  activation_list_matches_.clear();
  activation_level_ = ActivationLevel::DISABLED;
  measure_performance_ = false;
  aggregated_document_statistics_ = DocumentLoadStatistics();
}

void ContentSubresourceFilterDriverFactory::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame() &&
      !navigation_handle->IsSameDocument()) {
    activation_decision_ = ActivationDecision::UNKNOWN;
    ResetActivationState();
    navigation_chain_.push_back(navigation_handle->GetURL());
    client_->ToggleNotificationVisibility(false);
  }
}

void ContentSubresourceFilterDriverFactory::DidRedirectNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK(!navigation_handle->IsSameDocument());
  if (navigation_handle->IsInMainFrame())
    navigation_chain_.push_back(navigation_handle->GetURL());
}

void ContentSubresourceFilterDriverFactory::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  if (render_frame_host->GetParent())
    return;

  if (activation_level_ != ActivationLevel::DISABLED) {
    UMA_HISTOGRAM_COUNTS_1000(
        "SubresourceFilter.PageLoad.NumSubresourceLoads.Total",
        aggregated_document_statistics_.num_loads_total);
    UMA_HISTOGRAM_COUNTS_1000(
        "SubresourceFilter.PageLoad.NumSubresourceLoads.Evaluated",
        aggregated_document_statistics_.num_loads_evaluated);
    UMA_HISTOGRAM_COUNTS_1000(
        "SubresourceFilter.PageLoad.NumSubresourceLoads.MatchedRules",
        aggregated_document_statistics_.num_loads_matching_rules);
    UMA_HISTOGRAM_COUNTS_1000(
        "SubresourceFilter.PageLoad.NumSubresourceLoads.Disallowed",
        aggregated_document_statistics_.num_loads_disallowed);
  }

  if (measure_performance_) {
    DCHECK(activation_level_ != ActivationLevel::DISABLED);
    UMA_HISTOGRAM_CUSTOM_MICRO_TIMES(
        "SubresourceFilter.PageLoad.SubresourceEvaluation.TotalWallDuration",
        aggregated_document_statistics_.evaluation_total_wall_duration,
        base::TimeDelta::FromMicroseconds(1), base::TimeDelta::FromSeconds(10),
        50);
    UMA_HISTOGRAM_CUSTOM_MICRO_TIMES(
        "SubresourceFilter.PageLoad.SubresourceEvaluation.TotalCPUDuration",
        aggregated_document_statistics_.evaluation_total_cpu_duration,
        base::TimeDelta::FromMicroseconds(1), base::TimeDelta::FromSeconds(10),
        50);
  } else {
    DCHECK(aggregated_document_statistics_.evaluation_total_wall_duration
               .is_zero());
    DCHECK(aggregated_document_statistics_.evaluation_total_cpu_duration
               .is_zero());
  }
}

bool ContentSubresourceFilterDriverFactory::OnMessageReceived(
    const IPC::Message& message,
    content::RenderFrameHost* render_frame_host) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ContentSubresourceFilterDriverFactory, message)
    IPC_MESSAGE_HANDLER(SubresourceFilterHostMsg_DocumentLoadStatistics,
                        OnDocumentLoadStatistics)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

bool ContentSubresourceFilterDriverFactory::DidURLMatchActivationList(
    const GURL& url,
    ActivationList activation_list) const {
  auto match_types =
      activation_list_matches_.find(DistillURLToHostAndPath(url));
  return match_types != activation_list_matches_.end() &&
         match_types->second.find(activation_list) != match_types->second.end();
}

void ContentSubresourceFilterDriverFactory::AddActivationListMatch(
    const GURL& url,
    ActivationList match_type) {
  if (match_type == ActivationList::NONE)
    return;
  if (url.has_host() && url.SchemeIsHTTPOrHTTPS())
    activation_list_matches_[DistillURLToHostAndPath(url)].insert(match_type);
}

int ContentSubresourceFilterDriverFactory::CalculateHitPatternForActivationList(
    ActivationList activation_list) const {
  int hits_pattern = 0;
  const int kInitialURLHitMask = 0x4;
  const int kRedirectURLHitMask = 0x2;
  const int kFinalURLHitMask = 0x1;

  if (navigation_chain_.size() > 1) {
    if (DidURLMatchActivationList(navigation_chain_.back(), activation_list))
      hits_pattern |= kFinalURLHitMask;
    if (DidURLMatchActivationList(navigation_chain_.front(), activation_list))
      hits_pattern |= kInitialURLHitMask;

    // Examine redirects.
    for (size_t i = 1; i < navigation_chain_.size() - 1; ++i) {
      if (DidURLMatchActivationList(navigation_chain_[i], activation_list)) {
        hits_pattern |= kRedirectURLHitMask;
        break;
      }
    }
  } else {
    if (navigation_chain_.size() &&
        DidURLMatchActivationList(navigation_chain_.front(), activation_list)) {
      hits_pattern = 0x8;  // One url hit.
    }
  }
  return hits_pattern;
}

void ContentSubresourceFilterDriverFactory::RecordRedirectChainMatchPattern()
    const {
  RecordRedirectChainMatchPatternForList(
      ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL);
  RecordRedirectChainMatchPatternForList(ActivationList::PHISHING_INTERSTITIAL);
  RecordRedirectChainMatchPatternForList(ActivationList::SUBRESOURCE_FILTER);
}

void ContentSubresourceFilterDriverFactory::
    RecordRedirectChainMatchPatternForList(
        ActivationList activation_list) const {
  int hits_pattern = CalculateHitPatternForActivationList(activation_list);
  if (!hits_pattern)
    return;
  size_t chain_size = navigation_chain_.size();
  switch (activation_list) {
    case ActivationList::SOCIAL_ENG_ADS_INTERSTITIAL:
      REPORT_REDIRECT_PATTERN_FOR_SUFFIX("SocialEngineeringAdsInterstitial",
                                         hits_pattern, chain_size);
      break;
    case ActivationList::PHISHING_INTERSTITIAL:
      REPORT_REDIRECT_PATTERN_FOR_SUFFIX("PhishingInterstital", hits_pattern,
                                         chain_size);
      break;
    case ActivationList::SUBRESOURCE_FILTER:
      REPORT_REDIRECT_PATTERN_FOR_SUFFIX("SubresourceFilterOnly", hits_pattern,
                                         chain_size);
      break;
    default:
      NOTREACHED();
      break;
  }
}

}  // namespace subresource_filter
