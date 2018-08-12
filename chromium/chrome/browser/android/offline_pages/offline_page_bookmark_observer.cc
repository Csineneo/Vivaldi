// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/offline_pages/offline_page_bookmark_observer.h"

#include "chrome/browser/profiles/profile.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/offline_pages/client_namespace_constants.h"
#include "components/offline_pages/offline_page_model.h"
#include "url/gurl.h"

namespace offline_pages {

OfflinePageBookmarkObserver::OfflinePageBookmarkObserver(
    content::BrowserContext* context)
    : context_(context),
      offline_page_model_(nullptr),
      weak_ptr_factory_(this) {}

OfflinePageBookmarkObserver::~OfflinePageBookmarkObserver() {}

void OfflinePageBookmarkObserver::BookmarkModelChanged() {}

void OfflinePageBookmarkObserver::BookmarkNodeRemoved(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* parent,
    int old_index,
    const bookmarks::BookmarkNode* node,
    const std::set<GURL>& removed_urls) {
  if (!offline_page_model_) {
    offline_page_model_ =
        OfflinePageModelFactory::GetForBrowserContext(context_);
  }
  ClientId client_id = ClientId(kBookmarkNamespace, std::to_string(node->id()));
  offline_page_model_->GetOfflineIdsForClientId(
      client_id,
      base::Bind(&OfflinePageBookmarkObserver::DoExpireRemovedBookmarkPages,
                 weak_ptr_factory_.GetWeakPtr()));
}

void OfflinePageBookmarkObserver::DoExpireRemovedBookmarkPages(
    const MultipleOfflineIdResult& offline_ids) {
  offline_page_model_->ExpirePages(
      offline_ids, base::Time::Now(),
      base::Bind(&OfflinePageBookmarkObserver::OnExpireRemovedBookmarkPagesDone,
                 weak_ptr_factory_.GetWeakPtr()));
}

void OfflinePageBookmarkObserver::OnExpireRemovedBookmarkPagesDone(
    bool result) {}

}  // namespace offline_pages
