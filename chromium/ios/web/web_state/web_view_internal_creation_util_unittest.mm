// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/web_view_internal_creation_util.h"

#import <CoreGraphics/CoreGraphics.h>
#import <WebKit/WebKit.h>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "ios/web/net/request_group_util.h"
#include "ios/web/public/test/scoped_testing_web_client.h"
#include "ios/web/public/test/test_browser_state.h"
#import "ios/web/public/test/test_web_client.h"
#include "ios/web/public/test/test_web_thread.h"
#import "ios/web/public/web_view_creation_util.h"
#import "ios/web/test/web_test.h"
#import "ios/web/web_state/ui/wk_web_view_configuration_provider.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest_mac.h"

namespace web {
namespace {

const CGRect kTestFrame = CGRectMake(5.0f, 10.0f, 15.0f, 20.0f);

// A WebClient that stubs PreWebViewCreation/PostWebViewCreation calls for
// testing purposes.
class CreationUtilsWebClient : public TestWebClient {
 public:
  MOCK_CONST_METHOD0(PreWebViewCreation, void());
  MOCK_CONST_METHOD1(PostWebViewCreation, void(UIWebView* web_view));
};

class WebViewCreationUtilsTest : public WebTest {
 public:
  WebViewCreationUtilsTest()
      : web_client_(base::WrapUnique(new CreationUtilsWebClient)) {}

 protected:
  CreationUtilsWebClient* creation_utils_web_client() {
    return static_cast<CreationUtilsWebClient*>(web_client_.Get());
  }
  void SetUp() override {
    WebTest::SetUp();
    logJavaScriptPref_ =
        [[NSUserDefaults standardUserDefaults] boolForKey:@"LogJavascript"];
  }
  void TearDown() override {
    [[NSUserDefaults standardUserDefaults] setBool:logJavaScriptPref_
                                            forKey:@"LogJavascript"];
    WebTest::TearDown();
  }
  // Sets up expectation for WebClient::PreWebViewCreation and
  // WebClient::PostWebViewCreation calls. Captures UIWebView passed to
  // PostWebViewCreation into captured_web_view param.
  void ExpectWebClientCalls(UIWebView** captured_web_view) {
    EXPECT_CALL(*creation_utils_web_client(), PreWebViewCreation()).Times(1);
    EXPECT_CALL(*creation_utils_web_client(), PostWebViewCreation(testing::_))
        .Times(1)
        .WillOnce(testing::SaveArg<0>(captured_web_view));
  }

 private:
  // Original value of @"LogJavascript" pref from NSUserDefaults.
  BOOL logJavaScriptPref_;
  // WebClient that stubs PreWebViewCreation/PostWebViewCreation.
  web::ScopedTestingWebClient web_client_;
};

// Tests web::CreateWebView function that it correctly returns a UIWebView with
// the correct frame and calls WebClient::PreWebViewCreation/
// WebClient::PostWebViewCreation methods.
TEST_F(WebViewCreationUtilsTest, Creation) {
  [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"LogJavascript"];

  UIWebView* captured_web_view = nil;
  ExpectWebClientCalls(&captured_web_view);

  base::scoped_nsobject<UIWebView> web_view(CreateWebView(kTestFrame));
  EXPECT_TRUE([web_view isMemberOfClass:[UIWebView class]]);
  EXPECT_TRUE(CGRectEqualToRect(kTestFrame, [web_view frame]));
  EXPECT_NSEQ(web_view, captured_web_view);
}

// Tests web::CreateWKWebView function that it correctly returns a WKWebView
// with the correct frame and WKProcessPool.
TEST_F(WebViewCreationUtilsTest, WKWebViewCreationWithBrowserState) {
  base::scoped_nsobject<WKWebView> web_view(
      CreateWKWebView(kTestFrame, GetBrowserState()));

  EXPECT_TRUE([web_view isKindOfClass:[WKWebView class]]);
  EXPECT_TRUE(CGRectEqualToRect(kTestFrame, [web_view frame]));

  // Make sure that web view's configuration shares the same process pool with
  // browser state's configuration. Otherwise cookie will not be immediately
  // shared between different web views.
  WKWebViewConfigurationProvider& config_provider =
      WKWebViewConfigurationProvider::FromBrowserState(GetBrowserState());
  EXPECT_EQ(config_provider.GetWebViewConfiguration().processPool,
            [[web_view configuration] processPool]);
}

// Tests that web::CreateWKWebView always returns a web view with the same
// processPool.
TEST_F(WebViewCreationUtilsTest, WKWebViewsShareProcessPool) {
  base::scoped_nsobject<WKWebView> web_view(
      CreateWKWebView(kTestFrame, GetBrowserState()));
  ASSERT_TRUE(web_view);
  base::scoped_nsobject<WKWebView> web_view2(
      CreateWKWebView(kTestFrame, GetBrowserState()));
  ASSERT_TRUE(web_view2);

  // Make sure that web views share the same non-nil process pool. Otherwise
  // cookie will not be immediately shared between different web views.
  EXPECT_TRUE([[web_view configuration] processPool]);
  EXPECT_EQ([[web_view configuration] processPool],
            [[web_view2 configuration] processPool]);
}

}  // namespace
}  // namespace web
