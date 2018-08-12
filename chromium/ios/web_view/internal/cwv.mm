// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/public/cwv.h"

#include <memory>

#include "base/location.h"
#import "base/mac/bind_objc_block.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/sys_string_conversions.h"
#include "ios/web/public/app/web_main.h"
#include "ios/web/public/web_thread.h"
#import "ios/web_view/internal/web_view_web_main_delegate.h"
#import "ios/web_view/public/cwv_web_view.h"
#import "ios/web_view/public/cwv_web_view_configuration.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
CWV* g_criwv = nil;
}

@interface CWV () {
  std::unique_ptr<ios_web_view::WebViewWebMainDelegate> _webMainDelegate;
  std::unique_ptr<web::WebMain> _webMain;
}
@end

@implementation CWV

+ (void)configureWithUserAgentProductName:(NSString*)productName {
  g_criwv = [[CWV alloc] initWithUserAgentProductName:productName];
}

+ (void)shutDown {
  g_criwv = nil;
}

+ (CWVWebView*)webViewWithFrame:(CGRect)frame {
  CWVWebViewConfiguration* configuration =
      [CWVWebViewConfiguration defaultConfiguration];
  return [[CWVWebView alloc] initWithFrame:frame configuration:configuration];
}

- (instancetype)initWithUserAgentProductName:(NSString*)productName {
  self = [super init];
  if (self) {
    std::string userAgent = base::SysNSStringToUTF8(productName);
    _webMainDelegate =
        base::MakeUnique<ios_web_view::WebViewWebMainDelegate>(userAgent);
    web::WebMainParams params(_webMainDelegate.get());
    _webMain = base::MakeUnique<web::WebMain>(params);
  }
  return self;
}

- (void)dealloc {
  _webMain.reset();
  _webMainDelegate.reset();
}

@end
