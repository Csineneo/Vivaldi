// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/offline_pages/recent_tab_suggestions_provider.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/ntp_snippets/pref_names.h"
#include "components/ntp_snippets/pref_util.h"
#include "components/offline_pages/client_namespace_constants.h"
#include "components/offline_pages/offline_page_item.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image.h"

using offline_pages::ClientId;
using offline_pages::OfflinePageItem;

namespace ntp_snippets {

namespace {

const int kMaxSuggestionsCount = 5;

struct OrderOfflinePagesByMostRecentlyVisitedFirst {
  bool operator()(const OfflinePageItem* left,
                  const OfflinePageItem* right) const {
    return left->last_access_time > right->last_access_time;
  }
};

bool IsRecentTab(const ClientId& client_id) {
  return client_id.name_space == offline_pages::kLastNNamespace;
}

}  // namespace

RecentTabSuggestionsProvider::RecentTabSuggestionsProvider(
    ContentSuggestionsProvider::Observer* observer,
    CategoryFactory* category_factory,
    scoped_refptr<OfflinePageProxy> offline_page_proxy,
    PrefService* pref_service)
    : ContentSuggestionsProvider(observer, category_factory),
      category_status_(CategoryStatus::AVAILABLE_LOADING),
      provided_category_(
          category_factory->FromKnownCategory(KnownCategories::RECENT_TABS)),
      offline_page_proxy_(offline_page_proxy),
      pref_service_(pref_service),
      weak_ptr_factory_(this) {
  observer->OnCategoryStatusChanged(this, provided_category_, category_status_);
  offline_page_proxy_->AddObserver(this);
  FetchRecentTabs();
}

RecentTabSuggestionsProvider::~RecentTabSuggestionsProvider() {
  offline_page_proxy_->RemoveObserver(this);
}

CategoryStatus RecentTabSuggestionsProvider::GetCategoryStatus(
    Category category) {
  if (category == provided_category_)
    return category_status_;
  NOTREACHED() << "Unknown category " << category.id();
  return CategoryStatus::NOT_PROVIDED;
}

CategoryInfo RecentTabSuggestionsProvider::GetCategoryInfo(Category category) {
  if (category == provided_category_) {
    return CategoryInfo(l10n_util::GetStringUTF16(
                            IDS_NTP_RECENT_TAB_SUGGESTIONS_SECTION_HEADER),
                        ContentSuggestionsCardLayout::MINIMAL_CARD,
                        /*has_more_button=*/false,
                        /*show_if_empty=*/false);
  }
  NOTREACHED() << "Unknown category " << category.id();
  return CategoryInfo(base::string16(),
                      ContentSuggestionsCardLayout::MINIMAL_CARD,
                      /*has_more_button=*/false,
                      /*show_if_empty=*/false);
}

void RecentTabSuggestionsProvider::DismissSuggestion(
    const ContentSuggestion::ID& suggestion_id) {
  DCHECK_EQ(provided_category_, suggestion_id.category());
  std::set<std::string> dismissed_ids = ReadDismissedIDsFromPrefs();
  dismissed_ids.insert(suggestion_id.id_within_category());
  StoreDismissedIDsToPrefs(dismissed_ids);
}

void RecentTabSuggestionsProvider::FetchSuggestionImage(
    const ContentSuggestion::ID& suggestion_id,
    const ImageFetchedCallback& callback) {
  // TODO(vitaliii): Fetch proper thumbnail from OfflinePageModel once it's
  // available there.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(callback, gfx::Image()));
}

void RecentTabSuggestionsProvider::ClearHistory(
    base::Time begin,
    base::Time end,
    const base::Callback<bool(const GURL& url)>& filter) {
  ClearDismissedSuggestionsForDebugging(provided_category_);
  FetchRecentTabs();
}

void RecentTabSuggestionsProvider::ClearCachedSuggestions(Category category) {
  // Ignored.
}

void RecentTabSuggestionsProvider::GetDismissedSuggestionsForDebugging(
    Category category,
    const DismissedSuggestionsCallback& callback) {
  DCHECK_EQ(provided_category_, category);
  offline_page_proxy_->GetAllPages(
      base::Bind(&RecentTabSuggestionsProvider::
                     GetAllPagesCallbackForGetDismissedSuggestions,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void RecentTabSuggestionsProvider::ClearDismissedSuggestionsForDebugging(
    Category category) {
  DCHECK_EQ(provided_category_, category);
  StoreDismissedIDsToPrefs(std::set<std::string>());
  FetchRecentTabs();
}

// static
void RecentTabSuggestionsProvider::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterListPref(prefs::kDismissedRecentOfflineTabSuggestions);
}

////////////////////////////////////////////////////////////////////////////////
// Private methods

void RecentTabSuggestionsProvider::
    GetAllPagesCallbackForGetDismissedSuggestions(
        const DismissedSuggestionsCallback& callback,
        const std::vector<OfflinePageItem>& offline_pages) const {
  std::set<std::string> dismissed_ids = ReadDismissedIDsFromPrefs();
  std::vector<ContentSuggestion> suggestions;
  for (const OfflinePageItem& item : offline_pages) {
    if (!IsRecentTab(item.client_id) ||
        !dismissed_ids.count(base::IntToString(item.offline_id)))
      continue;
    suggestions.push_back(ConvertOfflinePage(item));
  }
  callback.Run(std::move(suggestions));
}

void RecentTabSuggestionsProvider::OfflinePageModelChanged(
    const std::vector<OfflinePageItem>& offline_pages) {
  NotifyStatusChanged(CategoryStatus::AVAILABLE);
  std::set<std::string> old_dismissed_ids = ReadDismissedIDsFromPrefs();
  std::set<std::string> new_dismissed_ids;
  std::vector<const OfflinePageItem*> recent_tab_items;
  for (const OfflinePageItem& item : offline_pages) {
    std::string offline_page_id = base::IntToString(item.offline_id);
    if (!IsRecentTab(item.client_id)) {
      continue;
    }

    if (old_dismissed_ids.count(offline_page_id))
      new_dismissed_ids.insert(offline_page_id);
    else
      recent_tab_items.push_back(&item);
  }

  observer()->OnNewSuggestions(
      this, provided_category_,
      GetMostRecentlyVisited(std::move(recent_tab_items)));
  if (new_dismissed_ids.size() != old_dismissed_ids.size())
    StoreDismissedIDsToPrefs(new_dismissed_ids);
}

void RecentTabSuggestionsProvider::OfflinePageDeleted(
    int64_t offline_id,
    const ClientId& client_id) {
  // Because we never switch to NOT_PROVIDED dynamically, there can be no open
  // UI containing an invalidated suggestion unless the status is something
  // other than NOT_PROVIDED, so only notify invalidation in that case.
  if (category_status_ != CategoryStatus::NOT_PROVIDED &&
      IsRecentTab(client_id)) {
    InvalidateSuggestion(offline_id);
  }
}

void RecentTabSuggestionsProvider::FetchRecentTabs() {
  // TODO(vitaliii): When something other than GetAllPages is used here, the
  // dismissed IDs cleanup in OfflinePageModelChanged needs to be changed to
  // avoid accidentally undismissing suggestions.
  offline_page_proxy_->GetAllPages(
      base::Bind(&RecentTabSuggestionsProvider::OfflinePageModelChanged,
                 weak_ptr_factory_.GetWeakPtr()));
}

void RecentTabSuggestionsProvider::NotifyStatusChanged(
    CategoryStatus new_status) {
  DCHECK_NE(CategoryStatus::NOT_PROVIDED, category_status_);
  if (category_status_ == new_status)
    return;
  category_status_ = new_status;
  observer()->OnCategoryStatusChanged(this, provided_category_, new_status);
}

ContentSuggestion RecentTabSuggestionsProvider::ConvertOfflinePage(
    const OfflinePageItem& offline_page) const {
  // TODO(vitaliii): Make sure the URL is opened in the existing tab.
  ContentSuggestion suggestion(provided_category_,
                               base::IntToString(offline_page.offline_id),
                               offline_page.url);

  if (offline_page.title.empty()) {
    // TODO(vitaliii): Remove this fallback once the OfflinePageModel provides
    // titles for all (relevant) OfflinePageItems.
    suggestion.set_title(base::UTF8ToUTF16(offline_page.url.spec()));
  } else {
    suggestion.set_title(offline_page.title);
  }
  suggestion.set_publish_date(offline_page.creation_time);
  suggestion.set_publisher_name(base::UTF8ToUTF16(offline_page.url.host()));
  return suggestion;
}

std::vector<ContentSuggestion>
RecentTabSuggestionsProvider::GetMostRecentlyVisited(
    std::vector<const OfflinePageItem*> offline_page_items) const {
  std::sort(offline_page_items.begin(), offline_page_items.end(),
            OrderOfflinePagesByMostRecentlyVisitedFirst());
  std::vector<ContentSuggestion> suggestions;
  for (const OfflinePageItem* offline_page_item : offline_page_items) {
    suggestions.push_back(ConvertOfflinePage(*offline_page_item));
    if (suggestions.size() == kMaxSuggestionsCount)
      break;
  }
  return suggestions;
}

void RecentTabSuggestionsProvider::InvalidateSuggestion(int64_t offline_id) {
  std::string offline_page_id = base::IntToString(offline_id);
  observer()->OnSuggestionInvalidated(
      this, ContentSuggestion::ID(provided_category_, offline_page_id));

  std::set<std::string> dismissed_ids = ReadDismissedIDsFromPrefs();
  auto it = dismissed_ids.find(offline_page_id);
  if (it != dismissed_ids.end()) {
    dismissed_ids.erase(it);
    StoreDismissedIDsToPrefs(dismissed_ids);
  }
}

std::set<std::string> RecentTabSuggestionsProvider::ReadDismissedIDsFromPrefs()
    const {
  return prefs::ReadDismissedIDsFromPrefs(
      *pref_service_, prefs::kDismissedRecentOfflineTabSuggestions);
}

void RecentTabSuggestionsProvider::StoreDismissedIDsToPrefs(
    const std::set<std::string>& dismissed_ids) {
  prefs::StoreDismissedIDsToPrefs(pref_service_,
                                  prefs::kDismissedRecentOfflineTabSuggestions,
                                  dismissed_ids);
}

}  // namespace ntp_snippets
