// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/test/histogram_tester.h"
#include "chrome/browser/page_load_metrics/observers/core_page_load_metrics_observer.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

class MetricsWebContentsObserverBrowserTest : public InProcessBrowserTest {
 public:
  MetricsWebContentsObserverBrowserTest() {}
  ~MetricsWebContentsObserverBrowserTest() override {}

 protected:
  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(MetricsWebContentsObserverBrowserTest);
};

IN_PROC_BROWSER_TEST_F(MetricsWebContentsObserverBrowserTest, NoNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 0);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 0);
}

IN_PROC_BROWSER_TEST_F(MetricsWebContentsObserverBrowserTest, NewPage) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title2.html"));

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
}

IN_PROC_BROWSER_TEST_F(MetricsWebContentsObserverBrowserTest, AnchorLink) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title1.html"));
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html#hash"));
  ui_test_utils::NavigateToURL(browser(),
                               embedded_test_server()->GetURL("/title2.html"));

  histogram_tester_.ExpectTotalCount(internal::kHistogramDomContentLoaded, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramLoad, 1);
  histogram_tester_.ExpectTotalCount(internal::kHistogramFirstLayout, 1);
}
