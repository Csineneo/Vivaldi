// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/clean/chrome/browser/ui/tools/tools_mediator.h"

#import "ios/clean/chrome/browser/ui/tools/tools_consumer.h"
#include "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#include "third_party/ocmock/gtest_support.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

class ToolsMediatorTest : public PlatformTest {
 protected:
  ToolsMediator* mediator_;
};

TEST_F(ToolsMediatorTest, TestSetConsumer) {
  id consumer = OCMProtocolMock(@protocol(ToolsConsumer));
  mediator_ = [[ToolsMediator alloc] initWithConsumer:consumer];

  [[consumer verify] setToolsMenuItems:[OCMArg any]];
}

}  // namespace
