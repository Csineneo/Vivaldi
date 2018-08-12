// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <EarlGrey/EarlGrey.h>

namespace web {

// Shorthand for GREYMatchers::matcherForWebViewContainingText.
id<GREYMatcher> webViewContainingText(NSString* text);

// Shorthand for GREYMatchers::matcherForAddressFieldEqualToText.
id<GREYMatcher> addressFieldText(NSString* text);

// Shorthand for GREYMatchers::matcherForBackButton.
id<GREYMatcher> backButton();

// Shorthand for GREYMatchers::matcherForForwardButton.
id<GREYMatcher> forwardButton();

// Shorthand for GREYMatchers::matcherForAddressField.
id<GREYMatcher> addressField();

}  // namespace web

@interface GREYMatchers (WebShellAdditions)

// Matcher for WKWebView containing |text|.
+ (id<GREYMatcher>)matcherForWebViewContainingText:(NSString*)text;

// Matcher for web shell address field text property equal to |text|.
+ (id<GREYMatcher>)matcherForAddressFieldEqualToText:(NSString*)text;

// Matcher for back button in web shell.
+ (id<GREYMatcher>)matcherForWebShellBackButton;

// Matcher for forward button in web shell.
+ (id<GREYMatcher>)matcherForWebShellForwardButton;

// Matcher for address field in web shell.
+ (id<GREYMatcher>)matcherForWebShellAddressField;

@end