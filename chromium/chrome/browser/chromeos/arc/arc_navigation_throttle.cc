// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/arc_navigation_throttle.h"

#include <algorithm>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram_macros.h"
#include "chrome/browser/chromeos/arc/page_transition_util.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/intent_helper/arc_intent_helper_bridge.h"
#include "components/arc/intent_helper/local_activity_resolver.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/page_transition_types.h"

namespace arc {

namespace {

constexpr uint32_t kMinVersionForHandleUrl = 2;
constexpr uint32_t kMinVersionForRequestUrlHandlerList = 2;
constexpr uint32_t kMinVersionForAddPreferredPackage = 7;

scoped_refptr<ActivityIconLoader> GetIconLoader() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ArcServiceManager* arc_service_manager = ArcServiceManager::Get();
  return arc_service_manager ? arc_service_manager->icon_loader() : nullptr;
}

// Compares the host name of the referrer and target URL to decide whether
// the navigation needs to be overriden.
bool ShouldOverrideUrlLoading(const GURL& previous_url,
                              const GURL& current_url) {
  // When the navigation is initiated in a web page where sending a referrer
  // is disabled, |previous_url| can be empty. In this case, we should open
  // it in the desktop browser.
  if (!previous_url.is_valid() || previous_url.is_empty())
    return false;

  // Also check |current_url| just in case.
  if (!current_url.is_valid() || current_url.is_empty()) {
    DVLOG(1) << "Unexpected URL: " << current_url << ", opening it in Chrome.";
    return false;
  }

  // Check the scheme for both |previous_url| and |current_url| since an
  // extension could have referred us (e.g. Google Docs).
  if (!current_url.SchemeIsHTTPOrHTTPS() ||
      !previous_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }

  return !net::registry_controlled_domains::SameDomainOrHost(
      current_url, previous_url,
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

}  // namespace

ArcNavigationThrottle::ArcNavigationThrottle(
    content::NavigationHandle* navigation_handle,
    const ShowIntentPickerCallback& show_intent_picker_cb)
    : content::NavigationThrottle(navigation_handle),
      show_intent_picker_callback_(show_intent_picker_cb),
      previous_user_action_(CloseReason::INVALID),
      weak_ptr_factory_(this) {}

ArcNavigationThrottle::~ArcNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
ArcNavigationThrottle::WillStartRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  starting_gurl_ = GetStartingGURL();
  return HandleRequest();
}

content::NavigationThrottle::ThrottleCheckResult
ArcNavigationThrottle::WillRedirectRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  switch (previous_user_action_) {
    case CloseReason::ERROR:
    case CloseReason::DIALOG_DEACTIVATED:
      // User dismissed the dialog, or some error occurred before. Don't
      // repeatedly pop up the dialog.
      return content::NavigationThrottle::PROCEED;

    case CloseReason::ALWAYS_PRESSED:
    case CloseReason::JUST_ONCE_PRESSED:
    case CloseReason::PREFERRED_ACTIVITY_FOUND:
      // We must never show the intent picker for the same throttle more than
      // once and we must considerate that we may have redirections within the
      // same ArcNavigationThrottle even after seeing the UI and selecting an
      // app to handle the navigation. This section can be reached iff the user
      // selected Chrome to continue the navigation, since Resume() tells the
      // throttle to continue with the chain of redirections.
      //
      // For example, by clicking a youtube link on gmail you can see the
      // following URLs, assume our |starting_gurl_| is "http://www.google.com":
      //
      // 1) https://www.google.com/url?hl=en&q=https://youtube.com/watch?v=fake
      // 2) https://youtube.com/watch?v=fake
      // 3) https://www.youtube.com/watch?v=fake
      //
      // 1) was caught via WillStartRequest() and 2) and 3) are caught via
      // WillRedirectRequest().Step 2) triggers the intent picker and step 3)
      // will be seen iff the user picks Chrome, or if Chrome was marked as the
      // preferred app for this kind of URL. This happens since after choosing
      // Chrome we tell the throttle to Resume(), thus allowing for further
      // redirections.
      return content::NavigationThrottle::PROCEED;

    case CloseReason::INVALID:
      // No picker has previously been popped up for this - continue.
      break;
  }
  return HandleRequest();
}

content::NavigationThrottle::ThrottleCheckResult
ArcNavigationThrottle::HandleRequest() {
  const GURL& url = navigation_handle()->GetURL();

  // Always handle http(s) <form> submissions in Chrome for two reasons: 1) we
  // don't have a way to send POST data to ARC, and 2) intercepting http(s) form
  // submissions is not very important because such submissions are usually
  // done within the same domain. ShouldOverrideUrlLoading() below filters out
  // such submissions anyway.
  constexpr bool kAllowFormSubmit = false;

  // We must not handle navigations started from the context menu.
  if (navigation_handle()->WasStartedFromContextMenu())
    return content::NavigationThrottle::PROCEED;

  if (ShouldIgnoreNavigation(navigation_handle()->GetPageTransition(),
                             kAllowFormSubmit))
    return content::NavigationThrottle::PROCEED;

  if (!ShouldOverrideUrlLoading(starting_gurl_, url))
    return content::NavigationThrottle::PROCEED;

  ArcServiceManager* arc_service_manager = ArcServiceManager::Get();
  DCHECK(arc_service_manager);
  scoped_refptr<LocalActivityResolver> local_resolver =
      arc_service_manager->activity_resolver();
  if (local_resolver->ShouldChromeHandleUrl(url)) {
    // Allow navigation to proceed if there isn't an android app that handles
    // the given URL.
    return content::NavigationThrottle::PROCEED;
  }

  auto* instance = ArcIntentHelperBridge::GetIntentHelperInstance(
      "RequestUrlHandlerList", kMinVersionForRequestUrlHandlerList);
  if (!instance)
    return content::NavigationThrottle::PROCEED;
  instance->RequestUrlHandlerList(
      url.spec(), base::Bind(&ArcNavigationThrottle::OnAppCandidatesReceived,
                             weak_ptr_factory_.GetWeakPtr()));
  return content::NavigationThrottle::DEFER;
}

GURL ArcNavigationThrottle::GetStartingGURL() const {
  // This helps us determine a reference GURL for the current NavigationHandle.
  // This is the order or preferrence: Referrer > LastCommittedURL > SiteURL,
  // GetSiteURL *should* only be used on very rare cases, e.g. when the
  // navigation goes from https: to http: on a new tab, thus losing the other
  // potential referrers.
  const GURL referrer_url = navigation_handle()->GetReferrer().url;
  if (referrer_url.is_valid() && !referrer_url.is_empty())
    return referrer_url;

  const GURL last_committed_url =
      navigation_handle()->GetWebContents()->GetLastCommittedURL();
  if (last_committed_url.is_valid() && !last_committed_url.is_empty())
    return last_committed_url;

  // Using |site_| from WebContents instead of NavigationHandle as a reference
  // for |starting_gurl_|, this is a workaround required for [M-55] given the
  // available interfaces at the classes mentioned.
  return navigation_handle()->GetWebContents()->GetSiteInstance()->GetSiteURL();
}

// We received the array of app candidates to handle this URL (even the Chrome
// app is included).
void ArcNavigationThrottle::OnAppCandidatesReceived(
    mojo::Array<mojom::IntentHandlerInfoPtr> handlers) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (handlers.empty() ||
      (handlers.size() == 1 && ArcIntentHelperBridge::IsIntentHelperPackage(
                                   handlers[0]->package_name))) {
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
    std::string package_name = handlers[i]->package_name;
    OnIntentPickerClosed(std::move(handlers), package_name,
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
    mojo::Array<mojom::IntentHandlerInfoPtr> handlers,
    std::unique_ptr<ActivityIconLoader::ActivityToIconsMap> icons) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<AppInfo> app_info;

  for (const auto& handler : handlers) {
    gfx::Image icon;
    const ActivityIconLoader::ActivityName activity(handler->package_name,
                                                    handler->activity_name);
    const auto it = icons->find(activity);

    app_info.emplace_back(
        AppInfo(it != icons->end() ? it->second.icon20 : gfx::Image(),
                handler->package_name, handler->name));
  }

  show_intent_picker_callback_.Run(
      navigation_handle()->GetWebContents(), app_info,
      base::Bind(&ArcNavigationThrottle::OnIntentPickerClosed,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&handlers)));
}

void ArcNavigationThrottle::OnIntentPickerClosed(
    mojo::Array<mojom::IntentHandlerInfoPtr> handlers,
    std::string selected_app_package,
    CloseReason close_reason) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const GURL& url = navigation_handle()->GetURL();
  content::NavigationHandle* handle = navigation_handle();
  previous_user_action_ = close_reason;

  // Make sure that the instance at least supports HandleUrl.
  auto* instance = ArcIntentHelperBridge::GetIntentHelperInstance(
      "HandleUrl", kMinVersionForHandleUrl);
  size_t selected_app_index = handlers.size();
  if (!instance) {
    close_reason = CloseReason::ERROR;
  } else if (close_reason == CloseReason::JUST_ONCE_PRESSED ||
             close_reason == CloseReason::ALWAYS_PRESSED ||
             close_reason == CloseReason::PREFERRED_ACTIVITY_FOUND) {
    // Since we are selecting an app by its package name, we need to locate it
    // on the |handlers| structure before sending the IPC to ARC.
    for (size_t i = 0; i < handlers.size(); ++i) {
      if (handlers[i]->package_name == selected_app_package) {
        selected_app_index = i;
        break;
      }
    }

    if (selected_app_index == handlers.size())
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
      // Call AddPreferredPackage if it is supported. Reusing the same
      // |instance| is okay.
      if (ArcIntentHelperBridge::GetIntentHelperInstance(
              "AddPreferredPackage", kMinVersionForAddPreferredPackage)) {
        instance->AddPreferredPackage(
            handlers[selected_app_index]->package_name);
      }
      // fall through.
    }
    case CloseReason::JUST_ONCE_PRESSED:
    case CloseReason::PREFERRED_ACTIVITY_FOUND: {
      if (ArcIntentHelperBridge::IsIntentHelperPackage(
              handlers[selected_app_index]->package_name)) {
        handle->Resume();
      } else {
        instance->HandleUrl(url.spec(), selected_app_package);
        handle->CancelDeferredNavigation(
            content::NavigationThrottle::CANCEL_AND_IGNORE);
        if (handle->GetWebContents()->GetController().IsInitialNavigation())
          handle->GetWebContents()->Close();
      }
      break;
    }
    case CloseReason::INVALID: {
      NOTREACHED();
      return;
    }
  }

  UMA_HISTOGRAM_ENUMERATION("Arc.IntentHandlerAction",
                            static_cast<int>(close_reason),
                            static_cast<int>(CloseReason::SIZE));
}

// static
bool ArcNavigationThrottle::ShouldOverrideUrlLoadingForTesting(
    const GURL& previous_url,
    const GURL& current_url) {
  return ShouldOverrideUrlLoading(previous_url, current_url);
}

}  // namespace arc
