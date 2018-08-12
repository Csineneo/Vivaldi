// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/offline_pages/offline_page_tab_helper.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/thread_task_runner_handle.h"
#include "chrome/browser/android/offline_pages/offline_page_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "net/base/network_change_notifier.h"
#include "ui/base/page_transition_types.h"

DEFINE_WEB_CONTENTS_USER_DATA_KEY(offline_pages::OfflinePageTabHelper);

namespace offline_pages {

OfflinePageTabHelper::OfflinePageTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      weak_ptr_factory_(this) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
}

OfflinePageTabHelper::~OfflinePageTabHelper() {}

void OfflinePageTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  GURL last_redirect_from_url_copy = last_redirect_from_url_;
  last_redirect_from_url_ = GURL();

  // Skips non-main frame.
  if (!navigation_handle->IsInMainFrame())
    return;

  // Redirecting to online version will only take effect when there is network
  // connection.
  if (net::NetworkChangeNotifier::IsOffline())
    return;

  // Ignore navigations that are forward or back transitions in the nav stack
  // which are not at the head of the stack.
  if (web_contents()->GetController().GetEntryCount() > 0 &&
      web_contents()->GetController().GetCurrentEntryIndex() != -1 &&
      (web_contents()->GetController().GetCurrentEntryIndex() <
       web_contents()->GetController().GetEntryCount() - 1)) {
    return;
  }

  // Skips if not loading an offline copy of saved page.
  GURL online_url = offline_pages::OfflinePageUtils::GetOnlineURLForOfflineURL(
      web_contents()->GetBrowserContext(), navigation_handle->GetURL());
  if (!online_url.is_valid())
    return;

  // Avoids looping between online and offline redirections.
  if (last_redirect_from_url_copy == online_url)
    return;
  last_redirect_from_url_ = navigation_handle->GetURL();

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&OfflinePageTabHelper::RedirectFromOfflineToOnline,
                 weak_ptr_factory_.GetWeakPtr(),
                 online_url));
}

void OfflinePageTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  GURL last_redirect_from_url_copy = last_redirect_from_url_;
  last_redirect_from_url_ = GURL();

  // Skips non-main frame or load failure other than no network.
  if (navigation_handle->GetNetErrorCode() != net::ERR_INTERNET_DISCONNECTED ||
      !navigation_handle->IsInMainFrame()) {
    return;
  }

  // Redirecting to offline version will only take effect when there is no
  // network connection.
  if (!net::NetworkChangeNotifier::IsOffline())
    return;

  // On a forward or back transition, don't affect the order of the nav stack.
  if (navigation_handle->GetPageTransition() ==
      ui::PAGE_TRANSITION_FORWARD_BACK) {
    return;
  }

  // Skips if not loading an online version of saved page.
  GURL offline_url = offline_pages::OfflinePageUtils::GetOfflineURLForOnlineURL(
      web_contents()->GetBrowserContext(), navigation_handle->GetURL());
  if (!offline_url.is_valid())
    return;

  // Avoids looping between online and offline redirections.
  if (last_redirect_from_url_copy == offline_url)
    return;
  last_redirect_from_url_ = navigation_handle->GetURL();

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&OfflinePageTabHelper::RedirectFromOnlineToOffline,
                 weak_ptr_factory_.GetWeakPtr(),
                 offline_url));
}

void OfflinePageTabHelper::RedirectFromOfflineToOnline(const GURL& online_url) {
  UMA_HISTOGRAM_COUNTS("OfflinePages.RedirectToOnlineCount", 1);
  content::NavigationController::LoadURLParams load_params(online_url);
  load_params.transition_type = ui::PAGE_TRANSITION_CLIENT_REDIRECT;
  web_contents()->GetController().LoadURLWithParams(load_params);
}

void OfflinePageTabHelper::RedirectFromOnlineToOffline(
    const GURL& offline_url) {
  UMA_HISTOGRAM_COUNTS("OfflinePages.RedirectToOfflineCount", 1);
  content::NavigationController::LoadURLParams load_params(offline_url);
  load_params.transition_type = ui::PAGE_TRANSITION_CLIENT_REDIRECT;
  web_contents()->GetController().LoadURLWithParams(load_params);
}

}  // namespace offline_pages
