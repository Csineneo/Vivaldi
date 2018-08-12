// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "content/public/test/browser_test.h"
#include "headless/public/headless_browser.h"
#include "headless/public/headless_web_contents.h"
#include "headless/test/headless_browser_test.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"

namespace headless {

IN_PROC_BROWSER_TEST_F(HeadlessBrowserTest, CreateAndDestroyWebContents) {
  std::unique_ptr<HeadlessWebContents> web_contents =
      browser()->CreateWebContents(GURL("about:blank"), gfx::Size(800, 600));
  EXPECT_TRUE(web_contents);
  // TODO(skyostil): Verify viewport dimensions once we can.
  web_contents.reset();
}

IN_PROC_BROWSER_TEST_F(HeadlessBrowserTest, CreateWithBadURL) {
  GURL bad_url("not_valid");
  std::unique_ptr<HeadlessWebContents> web_contents =
      browser()->CreateWebContents(bad_url, gfx::Size(800, 600));
  EXPECT_FALSE(web_contents);
}

class HeadlessBrowserTestWithProxy : public HeadlessBrowserTest {
 public:
  HeadlessBrowserTestWithProxy()
      : proxy_server_(net::SpawnedTestServer::TYPE_HTTP,
                      net::SpawnedTestServer::kLocalhost,
                      base::FilePath(FILE_PATH_LITERAL("headless/test/data"))) {
  }

  void SetUp() override {
    ASSERT_TRUE(proxy_server_.Start());
    HeadlessBrowserTest::SetUp();
  }

  void TearDown() override {
    proxy_server_.Stop();
    HeadlessBrowserTest::TearDown();
  }

  net::SpawnedTestServer* proxy_server() { return &proxy_server_; }

 private:
  net::SpawnedTestServer proxy_server_;
};

IN_PROC_BROWSER_TEST_F(HeadlessBrowserTestWithProxy, SetProxyServer) {
  HeadlessBrowser::Options::Builder builder;
  builder.SetProxyServer(proxy_server()->host_port_pair());
  SetBrowserOptions(builder.Build());

  // Load a page which doesn't actually exist, but for which the our proxy
  // returns valid content anyway.
  std::unique_ptr<HeadlessWebContents> web_contents =
      browser()->CreateWebContents(
          GURL("http://not-an-actual-domain.tld/hello.html"),
          gfx::Size(800, 600));
  EXPECT_TRUE(WaitForLoad(web_contents.get()));
}

}  // namespace headless
