// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/arc_navigation_throttle.h"

#include <algorithm>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram_macros.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/intent_helper/arc_intent_helper_bridge.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "ui/base/page_transition_types.h"

namespace arc {

namespace {

constexpr int kMinInstanceVersion = 7;

scoped_refptr<ActivityIconLoader> GetIconLoader() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ArcServiceManager* arc_service_manager = ArcServiceManager::Get();
  if (!arc_service_manager)
    return nullptr;
  return arc_service_manager->icon_loader();
}

mojom::IntentHelperInstance* GetIntentHelper() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ArcBridgeService* bridge_service = ArcBridgeService::Get();
  if (!bridge_service) {
    VLOG(1) << "ARC bridge is not ready.";
    return nullptr;
  }
  mojom::IntentHelperInstance* intent_helper_instance =
      bridge_service->intent_helper()->instance();
  if (!intent_helper_instance) {
    VLOG(1) << "ARC intent helper instance is not ready.";
    return nullptr;
  }
  if (bridge_service->intent_helper()->version() < kMinInstanceVersion) {
    VLOG(1) << "ARC intent helper instance is too old.";
    return nullptr;
  }
  return intent_helper_instance;
}

}  // namespace

ArcNavigationThrottle::ArcNavigationThrottle(
    content::NavigationHandle* navigation_handle,
    const ShowDisambigDialogCallback& show_disambig_dialog_cb)
    : content::NavigationThrottle(navigation_handle),
      show_disambig_dialog_callback_(show_disambig_dialog_cb),
      weak_ptr_factory_(this) {}

ArcNavigationThrottle::~ArcNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
ArcNavigationThrottle::WillStartRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const ui::PageTransition transition =
      navigation_handle()->GetPageTransition();

  if (!ui::PageTransitionCoreTypeIs(transition, ui::PAGE_TRANSITION_LINK)) {
    // If this navigation event wasn't spawned by the user clicking on a link.
    return content::NavigationThrottle::PROCEED;
  }

  if (ui::PageTransitionGetQualifier(transition) != 0) {
    // Qualifiers indicate that this navigation was the result of a click on a
    // forward/back button, or a redirect, or typing in the URL bar, etc.  Don't
    // pass any of those types of navigations to the intent helper (see
    // crbug.com/630072).
    return content::NavigationThrottle::PROCEED;
  }

  const GURL& url = navigation_handle()->GetURL();
  mojom::IntentHelperInstance* bridge_instance = GetIntentHelper();
  if (!bridge_instance)
    return content::NavigationThrottle::PROCEED;
  bridge_instance->RequestUrlHandlerList(
      url.spec(), base::Bind(&ArcNavigationThrottle::OnAppCandidatesReceived,
                             weak_ptr_factory_.GetWeakPtr()));
  return content::NavigationThrottle::DEFER;
}

content::NavigationThrottle::ThrottleCheckResult
ArcNavigationThrottle::WillRedirectRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return content::NavigationThrottle::PROCEED;
}

// We received the array of app candidates to handle this URL (even the Chrome
// app is included).
void ArcNavigationThrottle::OnAppCandidatesReceived(
    mojo::Array<mojom::UrlHandlerInfoPtr> handlers) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if ((handlers.size() == 1 && ArcIntentHelperBridge::IsIntentHelperPackage(
                                   handlers[0]->package_name)) ||
      handlers.size() == 0) {
    // This scenario shouldn't be accesed as ArcNavigationThrottle is created
    // iff there are ARC apps which can actually handle the given URL.
    DVLOG(1) << "There are no app candidates for this URL: "
             << navigation_handle()->GetURL().spec();
    navigation_handle()->Resume();
    return;
  }

  // If one of the apps is marked as preferred, use it right away without
  // showing the UI.
  for (size_t i = 0; i < handlers.size(); ++i) {
    if (!handlers[i]->is_preferred)
      continue;
    if (ArcIntentHelperBridge::IsIntentHelperPackage(
            handlers[i]->package_name)) {
      // If Chrome browser was selected as the preferred app, we should't
      // create a throttle.
      DVLOG(1)
          << "Chrome browser is selected as the preferred app for this URL: "
          << navigation_handle()->GetURL().spec();
    }
    OnDisambigDialogClosed(std::move(handlers), i,
                           CloseReason::PREFERRED_ACTIVITY_FOUND);
    return;
  }

  // Swap Chrome app with any app in row |kMaxAppResults-1| iff his index is
  // bigger, thus ensuring the user can always see Chrome without scrolling.
  size_t chrome_app_index = 0;
  for (size_t i = 0; i < handlers.size(); ++i) {
    if (ArcIntentHelperBridge::IsIntentHelperPackage(
            handlers[i]->package_name)) {
      chrome_app_index = i;
      break;
    }
  }

  if (chrome_app_index >= kMaxAppResults)
    std::swap(handlers[kMaxAppResults - 1], handlers[chrome_app_index]);

  scoped_refptr<ActivityIconLoader> icon_loader = GetIconLoader();
  if (!icon_loader) {
    LOG(ERROR) << "Cannot get an instance of ActivityIconLoader";
    navigation_handle()->Resume();
    return;
  }
  std::vector<ActivityIconLoader::ActivityName> activities;
  for (const auto& handler : handlers) {
    activities.emplace_back(handler->package_name, handler->activity_name);
  }
  icon_loader->GetActivityIcons(
      activities,
      base::Bind(&ArcNavigationThrottle::OnAppIconsReceived,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&handlers)));
}

void ArcNavigationThrottle::OnAppIconsReceived(
    mojo::Array<mojom::UrlHandlerInfoPtr> handlers,
    std::unique_ptr<ActivityIconLoader::ActivityToIconsMap> icons) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<NameAndIcon> app_info;

  for (const auto& handler : handlers) {
    gfx::Image icon;
    const ActivityIconLoader::ActivityName activity(handler->package_name,
                                                    handler->activity_name);
    const auto it = icons->find(activity);

    if (it != icons->end()) {
      app_info.emplace_back(handler->name, it->second.icon20);
    } else {
      app_info.emplace_back(handler->name, gfx::Image());
    }
  }

  show_disambig_dialog_callback_.Run(
      navigation_handle(), app_info,
      base::Bind(&ArcNavigationThrottle::OnDisambigDialogClosed,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&handlers)));
}

void ArcNavigationThrottle::OnDisambigDialogClosed(
    mojo::Array<mojom::UrlHandlerInfoPtr> handlers,
    size_t selected_app_index,
    CloseReason close_reason) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const GURL& url = navigation_handle()->GetURL();
  content::NavigationHandle* handle = navigation_handle();

  mojom::IntentHelperInstance* bridge = GetIntentHelper();
  if (!bridge || selected_app_index >= handlers.size()) {
    close_reason = CloseReason::ERROR;
  }

  switch (close_reason) {
    case CloseReason::ERROR:
    case CloseReason::DIALOG_DEACTIVATED: {
      // If the user fails to select an option from the list, or the UI returned
      // an error or if |selected_app_index| is not a valid index, then resume
      // the navigation in Chrome.
      DVLOG(1) << "User didn't select a valid option, resuming navigation.";
      handle->Resume();
      break;
    }
    case CloseReason::ALWAYS_PRESSED: {
      bridge->AddPreferredPackage(handlers[selected_app_index]->package_name);
      // fall through.
    }
    case CloseReason::JUST_ONCE_PRESSED:
    case CloseReason::PREFERRED_ACTIVITY_FOUND: {
      if (ArcIntentHelperBridge::IsIntentHelperPackage(
              handlers[selected_app_index]->package_name)) {
        handle->Resume();
      } else {
        bridge->HandleUrl(url.spec(),
                          handlers[selected_app_index]->package_name);
        handle->CancelDeferredNavigation(
            content::NavigationThrottle::CANCEL_AND_IGNORE);
      }
      break;
    }
    case CloseReason::SIZE: {
      NOTREACHED();
      return;
    }
  }

  UMA_HISTOGRAM_ENUMERATION("Arc.IntentHandlerAction",
                            static_cast<int>(close_reason),
                            static_cast<int>(CloseReason::SIZE));
}

}  // namespace arc
