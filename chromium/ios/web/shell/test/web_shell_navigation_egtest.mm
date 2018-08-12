// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
#import <XCTest/XCTest.h>

#import <EarlGrey/EarlGrey.h>

#include "base/strings/sys_string_conversions.h"
#import "ios/web/public/test/http_server.h"
#include "ios/web/public/test/http_server_util.h"
#include "ios/web/shell/test/app/navigation_test_util.h"
#include "ios/web/shell/test/app/web_view_interaction_test_util.h"
#import "ios/web/shell/test/earl_grey/shell_matchers.h"

// Navigation test cases for the web shell. These are Earl Grey integration
// tests, which are based on XCTest.
@interface CRWWebShellNavigationTest : XCTestCase

@end

@implementation CRWWebShellNavigationTest

// Set up called once for the class.
+ (void)setUp {
  [super setUp];
  [[EarlGrey selectElementWithMatcher:web::webViewContainingText(@"Chromium")]
      assertWithMatcher:grey_notNil()];
  web::test::HttpServer& server = web::test::HttpServer::GetSharedInstance();
  server.StartOrDie();
  DCHECK(server.IsRunning());
}

// Tear down called once for the class.
+ (void)tearDown {
  [super tearDown];
  web::test::HttpServer& server = web::test::HttpServer::GetSharedInstance();
  server.Stop();
  DCHECK(!server.IsRunning());
}

// Tear down called after each test.
- (void)tearDown {
  [super tearDown];
  web::test::HttpServer& server = web::test::HttpServer::GetSharedInstance();
  server.RemoveAllResponseProviders();
}

// Tests clicking a link to about:blank.
- (void)testNavigationLinkToAboutBlank {
  const GURL URL = web::test::HttpServer::MakeUrl(
      "http://ios/web/shell/test/http_server_files/basic_navigation_test.html");
  NSString* URLSpec = base::SysUTF8ToNSString(URL.spec());
  web::test::SetUpFileBasedHttpServer();

  // TODO(crbug.com/611515): Create web shell utility that only requires URL,
  // and gets the web state and passes it in to the web view utility.
  web::shell_test_util::LoadUrl(URL);
  [[EarlGrey selectElementWithMatcher:web::addressFieldText(URLSpec)]
      assertWithMatcher:grey_notNil()];

  web::shell_test_util::TapWebViewElementWithId(
      "basic-link-navigation-to-about-blank");

  [[EarlGrey selectElementWithMatcher:web::addressFieldText(@"about:blank")]
      assertWithMatcher:grey_notNil()];
}

// Tests the back and forward button after entering two URLs.
- (void)testWebScenarioBrowsingBackAndForward {
  // Create map of canned responses and set up the test HTML server.
  std::map<GURL, std::string> responses;
  const GURL URL1 = web::test::HttpServer::MakeUrl("http://firstURL");
  NSString* URL1Text = base::SysUTF8ToNSString(URL1.spec());
  NSString* response1 = @"Test Page 1";
  responses[URL1] = base::SysNSStringToUTF8(response1);

  const GURL URL2 = web::test::HttpServer::MakeUrl("http://secondURL");
  NSString* URL2Text = base::SysUTF8ToNSString(URL2.spec());
  NSString* response2 = @"Test Page 2";
  responses[URL2] = base::SysNSStringToUTF8(response2);

  web::test::SetUpSimpleHttpServer(responses);

  web::shell_test_util::LoadUrl(URL1);
  [[EarlGrey selectElementWithMatcher:web::addressFieldText(URL1Text)]
      assertWithMatcher:grey_notNil()];
  [[EarlGrey selectElementWithMatcher:web::webViewContainingText(response1)]
      assertWithMatcher:grey_notNil()];

  web::shell_test_util::LoadUrl(URL2);
  [[EarlGrey selectElementWithMatcher:web::addressFieldText(URL2Text)]
      assertWithMatcher:grey_notNil()];
  [[EarlGrey selectElementWithMatcher:web::webViewContainingText(response2)]
      assertWithMatcher:grey_notNil()];

  [[EarlGrey selectElementWithMatcher:web::backButton()]
      performAction:grey_tap()];
  [[EarlGrey selectElementWithMatcher:web::addressFieldText(URL1Text)]
      assertWithMatcher:grey_notNil()];
  [[EarlGrey selectElementWithMatcher:web::webViewContainingText(response1)]
      assertWithMatcher:grey_notNil()];

  [[EarlGrey selectElementWithMatcher:web::forwardButton()]
      performAction:grey_tap()];
  [[EarlGrey selectElementWithMatcher:web::addressFieldText(URL2Text)]
      assertWithMatcher:grey_notNil()];
  [[EarlGrey selectElementWithMatcher:web::webViewContainingText(response2)]
      assertWithMatcher:grey_notNil()];
}

@end
