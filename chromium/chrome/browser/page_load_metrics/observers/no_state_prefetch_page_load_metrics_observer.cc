// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/no_state_prefetch_page_load_metrics_observer.h"

#include "chrome/browser/page_load_metrics/metrics_web_contents_observer.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"

// static
std::unique_ptr<NoStatePrefetchPageLoadMetricsObserver>
NoStatePrefetchPageLoadMetricsObserver::CreateIfNeeded(
    content::WebContents* web_contents) {
  prerender::PrerenderManager* manager =
      prerender::PrerenderManagerFactory::GetForBrowserContext(
          web_contents->GetBrowserContext());
  if (!manager)
    return nullptr;
  return base::MakeUnique<NoStatePrefetchPageLoadMetricsObserver>(manager);
}

NoStatePrefetchPageLoadMetricsObserver::NoStatePrefetchPageLoadMetricsObserver(
    prerender::PrerenderManager* manager)
    : is_no_store_(false), prerender_manager_(manager) {
  DCHECK(prerender_manager_);
}

NoStatePrefetchPageLoadMetricsObserver::
    ~NoStatePrefetchPageLoadMetricsObserver() {}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
NoStatePrefetchPageLoadMetricsObserver::OnCommit(
    content::NavigationHandle* navigation_handle) {
  const net::HttpResponseHeaders* response_headers =
      navigation_handle->GetResponseHeaders();

  is_no_store_ = response_headers &&
                 response_headers->HasHeaderValue("cache-control", "no-store");
  return CONTINUE_OBSERVING;
}

void NoStatePrefetchPageLoadMetricsObserver::OnFirstContentfulPaint(
    const page_load_metrics::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& extra_info) {
  DCHECK(timing.first_contentful_paint.has_value());
  prerender_manager_->RecordFirstContentfulPaint(
      extra_info.start_url, is_no_store_, *timing.first_contentful_paint);
}
