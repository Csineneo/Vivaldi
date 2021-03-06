// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/live_tab_count_page_load_metrics_observer.h"

#include <array>
#include <memory>
#include <string>

#include "base/test/metrics/histogram_tester.h"
#include "chrome/browser/metrics/live_tab_count_metrics.h"
#include "chrome/browser/page_load_metrics/observers/histogram_suffixes.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_test_waiter.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/live_tab_count_metrics/live_tab_count_metrics.h"
#include "testing/gtest/include/gtest/gtest.h"

using BucketCountArray =
    std::array<size_t, live_tab_count_metrics::kNumLiveTabCountBuckets>;
using page_load_metrics::PageLoadMetricsTestWaiter;
using TimingField = page_load_metrics::PageLoadMetricsTestWaiter::TimingField;

class LiveTabCountPageLoadMetricsBrowserTest : public InProcessBrowserTest {
 public:
  LiveTabCountPageLoadMetricsBrowserTest() = default;

  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL GetTestURL() { return embedded_test_server()->GetURL("/title1.html"); }

  std::unique_ptr<PageLoadMetricsTestWaiter>
  CreatePageLoadMetricsTestWaiterForForegroundTab() {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    return std::make_unique<PageLoadMetricsTestWaiter>(web_contents);
  }

  void ValidateHistograms(const char* suffix,
                          BucketCountArray& expected_counts) {
    const std::string histogram_prefix =
        std::string(internal::kHistogramPrefixLiveTabCount) +
        std::string(suffix);
    for (size_t bucket = 0; bucket < expected_counts.size(); bucket++) {
      histogram_tester_.ExpectTotalCount(
          live_tab_count_metrics::HistogramName(histogram_prefix, bucket),
          expected_counts[bucket]);
    }
  }

  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(LiveTabCountPageLoadMetricsBrowserTest);
};

IN_PROC_BROWSER_TEST_F(LiveTabCountPageLoadMetricsBrowserTest,
                       LoadSingleTabInForeground) {
  BucketCountArray counts = {0};

  auto waiter = CreatePageLoadMetricsTestWaiterForForegroundTab();
  waiter->AddPageExpectation(TimingField::kFirstContentfulPaint);
  waiter->AddPageExpectation(TimingField::kFirstMeaningfulPaint);

  ui_test_utils::NavigateToURL(browser(), GetTestURL());
  waiter->Wait();

  size_t live_tab_count = live_tab_count_metrics::LiveTabCount();
  EXPECT_EQ(live_tab_count, 1u);
  ++counts[live_tab_count_metrics::BucketForLiveTabCount(live_tab_count)];
  ValidateHistograms(internal::kHistogramFirstContentfulPaintSuffix, counts);
  ValidateHistograms(internal::kHistogramFirstMeaningfulPaintSuffix, counts);
}

IN_PROC_BROWSER_TEST_F(LiveTabCountPageLoadMetricsBrowserTest,
                       LoadSingleTabInBackground) {
  // Open a tab in the background, but don't wait for it to load; we need its
  // WebContents to create a PageLoadMetricsTestWaiter.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GetTestURL(), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_NONE);

  auto* web_contents = browser()->tab_strip_model()->GetWebContentsAt(1);
  EXPECT_TRUE(web_contents);
  auto waiter = std::make_unique<PageLoadMetricsTestWaiter>(web_contents);
  waiter->AddPageExpectation(TimingField::kFirstContentfulPaint);
  waiter->AddPageExpectation(TimingField::kFirstMeaningfulPaint);

  // Switch tabs so the paint events occur.
  browser()->tab_strip_model()->ActivateTabAt(
      1, {TabStripModel::GestureType::kOther});

  waiter->Wait();

  BucketCountArray counts = {0};
  ValidateHistograms(internal::kHistogramFirstContentfulPaintSuffix, counts);
  ValidateHistograms(internal::kHistogramFirstMeaningfulPaintSuffix, counts);
}

IN_PROC_BROWSER_TEST_F(LiveTabCountPageLoadMetricsBrowserTest,
                       LoadMultipleTabsInForeground) {
  // Test opening 5 tabs, which spans the first few buckets.
  constexpr size_t num_test_tabs = 5;

  BucketCountArray counts = {0};

  // Load the first tab separately, without inserting a new tab.
  auto waiter = CreatePageLoadMetricsTestWaiterForForegroundTab();
  waiter->AddPageExpectation(TimingField::kFirstContentfulPaint);
  waiter->AddPageExpectation(TimingField::kFirstMeaningfulPaint);

  ui_test_utils::NavigateToURL(browser(), GetTestURL());
  waiter->Wait();

  size_t live_tab_count = live_tab_count_metrics::LiveTabCount();
  EXPECT_EQ(live_tab_count, 1u);
  ++counts[live_tab_count_metrics::BucketForLiveTabCount(live_tab_count)];
  ValidateHistograms(internal::kHistogramFirstContentfulPaintSuffix, counts);
  ValidateHistograms(internal::kHistogramFirstMeaningfulPaintSuffix, counts);

  // Insert new tabs for the rest.
  for (size_t tab = 1; tab < num_test_tabs; tab++) {
    // Create the tab, but don't wait for it to load; we need its WebContents to
    // create a PageLoadMetricsTestWaiter.
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), GetTestURL(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_NONE);

    auto* web_contents = browser()->tab_strip_model()->GetWebContentsAt(tab);
    EXPECT_TRUE(web_contents);
    waiter.reset(new PageLoadMetricsTestWaiter(web_contents));
    waiter->AddPageExpectation(TimingField::kFirstContentfulPaint);
    waiter->AddPageExpectation(TimingField::kFirstMeaningfulPaint);

    waiter->Wait();

    live_tab_count = live_tab_count_metrics::LiveTabCount();
    EXPECT_EQ(live_tab_count, tab + 1);
    ++counts[live_tab_count_metrics::BucketForLiveTabCount(live_tab_count)];

    ValidateHistograms(internal::kHistogramFirstContentfulPaintSuffix, counts);
    ValidateHistograms(internal::kHistogramFirstMeaningfulPaintSuffix, counts);
  }
}
