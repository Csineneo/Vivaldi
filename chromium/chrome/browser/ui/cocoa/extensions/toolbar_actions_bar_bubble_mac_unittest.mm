// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#import "base/mac/foundation_util.h"
#import "base/mac/scoped_nsobject.h"
#include "base/strings/utf_string_conversions.h"
#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#import "chrome/browser/ui/cocoa/extensions/toolbar_actions_bar_bubble_mac.h"
#import "chrome/browser/ui/cocoa/run_loop_testing.h"
#include "chrome/browser/ui/toolbar/test_toolbar_actions_bar_bubble_delegate.h"
#import "ui/events/test/cocoa_test_event_utils.h"

// A simple class to observe when a window is destructing.
@interface WindowObserver : NSObject {
  BOOL windowIsClosing_;
}

- (id)initWithWindow:(NSWindow*)window;

- (void)dealloc;

- (void)onWindowClosing:(NSNotification*)notification;

@property(nonatomic, assign) BOOL windowIsClosing;

@end

@implementation WindowObserver

- (id)initWithWindow:(NSWindow*)window {
  if ((self = [super init])) {
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(onWindowClosing:)
               name:NSWindowWillCloseNotification
             object:window];
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (void)onWindowClosing:(NSNotification*)notification {
  windowIsClosing_ = YES;
}

@synthesize windowIsClosing = windowIsClosing_;

@end

class ToolbarActionsBarBubbleMacTest : public CocoaTest {
 public:
  ToolbarActionsBarBubbleMacTest() {}
  ~ToolbarActionsBarBubbleMacTest() override {}

  void SetUp() override;

  // Create and display a new bubble with the given |delegate|.
  ToolbarActionsBarBubbleMac* CreateAndShowBubble(
      TestToolbarActionsBarBubbleDelegate* delegate);

  // Test that clicking on the corresponding button produces the
  // |expected_action|, and closes the bubble.
  void TestBubbleButton(
      ToolbarActionsBarBubbleDelegate::CloseAction expected_action);

  base::string16 HeadingString() { return base::ASCIIToUTF16("Heading"); }
  base::string16 BodyString() { return base::ASCIIToUTF16("Body"); }
  base::string16 ActionString() { return base::ASCIIToUTF16("Action"); }
  base::string16 DismissString() { return base::ASCIIToUTF16("Dismiss"); }
  base::string16 LearnMoreString() { return base::ASCIIToUTF16("LearnMore"); }
  base::string16 ItemListString() { return base::ASCIIToUTF16("ItemList"); }

 private:
  DISALLOW_COPY_AND_ASSIGN(ToolbarActionsBarBubbleMacTest);
};

void ToolbarActionsBarBubbleMacTest::SetUp() {
  CocoaTest::SetUp();
  [ToolbarActionsBarBubbleMac setAnimationEnabledForTesting:NO];
}

ToolbarActionsBarBubbleMac* ToolbarActionsBarBubbleMacTest::CreateAndShowBubble(
    TestToolbarActionsBarBubbleDelegate* delegate) {
  ToolbarActionsBarBubbleMac* bubble =
      [[ToolbarActionsBarBubbleMac alloc]
          initWithParentWindow:test_window()
                   anchorPoint:NSZeroPoint
                      delegate:delegate->GetDelegate()];
  EXPECT_FALSE(delegate->shown());
  [bubble showWindow:nil];
  chrome::testing::NSRunLoopRunAllPending();
  EXPECT_FALSE(delegate->close_action());
  EXPECT_TRUE(delegate->shown());
  return bubble;
}

void ToolbarActionsBarBubbleMacTest::TestBubbleButton(
    ToolbarActionsBarBubbleDelegate::CloseAction expected_action) {
  TestToolbarActionsBarBubbleDelegate delegate(
      HeadingString(), BodyString(), ActionString());
  delegate.set_dismiss_button_text(DismissString());
  delegate.set_learn_more_button_text(LearnMoreString());
  ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
  base::scoped_nsobject<WindowObserver> windowObserver(
      [[WindowObserver alloc] initWithWindow:[bubble window]]);
  EXPECT_FALSE([windowObserver windowIsClosing]);

  // Find the appropriate button to click.
  NSButton* button = nil;
  switch (expected_action) {
    case ToolbarActionsBarBubbleDelegate::CLOSE_EXECUTE:
      button = [bubble actionButton];
      break;
    case ToolbarActionsBarBubbleDelegate::CLOSE_DISMISS:
      button = [bubble dismissButton];
      break;
    case ToolbarActionsBarBubbleDelegate::CLOSE_LEARN_MORE:
      button = [bubble learnMoreButton];
      break;
  }
  ASSERT_TRUE(button);

  // Click the button.
  std::pair<NSEvent*, NSEvent*> events =
      cocoa_test_event_utils::MouseClickInView(button, 1);
  [NSApp postEvent:events.second atStart:YES];
  [NSApp sendEvent:events.first];
  chrome::testing::NSRunLoopRunAllPending();

  // The bubble should be closed, and the delegate should be told that the
  // button was clicked.
  ASSERT_TRUE(delegate.close_action());
  EXPECT_EQ(expected_action, *delegate.close_action());
  EXPECT_TRUE([windowObserver windowIsClosing]);
}

// Test clicking on the action button and dismissing the bubble.
TEST_F(ToolbarActionsBarBubbleMacTest, CloseActionAndDismiss) {
  // Test all the possible actions.
  TestBubbleButton(ToolbarActionsBarBubbleDelegate::CLOSE_EXECUTE);
  TestBubbleButton(ToolbarActionsBarBubbleDelegate::CLOSE_DISMISS);
  TestBubbleButton(ToolbarActionsBarBubbleDelegate::CLOSE_LEARN_MORE);

  {
    // Test dismissing the bubble without clicking the button.
    TestToolbarActionsBarBubbleDelegate delegate(
        HeadingString(), BodyString(), ActionString());
    ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
    base::scoped_nsobject<WindowObserver> windowObserver(
        [[WindowObserver alloc] initWithWindow:[bubble window]]);
    EXPECT_FALSE([windowObserver windowIsClosing]);

    // Close the bubble. The delegate should be told it was dismissed.
    [bubble close];
    chrome::testing::NSRunLoopRunAllPending();
    ASSERT_TRUE(delegate.close_action());
    EXPECT_EQ(ToolbarActionsBarBubbleDelegate::CLOSE_DISMISS,
              *delegate.close_action());
    EXPECT_TRUE([windowObserver windowIsClosing]);
  }
}

// Test the basic layout of the bubble.
TEST_F(ToolbarActionsBarBubbleMacTest, ToolbarActionsBarBubbleLayout) {
  // Test with no optional fields.
  {
    TestToolbarActionsBarBubbleDelegate delegate(
        HeadingString(), BodyString(), ActionString());
    ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
    EXPECT_TRUE([bubble actionButton]);
    EXPECT_FALSE([bubble learnMoreButton]);
    EXPECT_FALSE([bubble dismissButton]);
    EXPECT_FALSE([bubble itemList]);

    [bubble close];
    chrome::testing::NSRunLoopRunAllPending();
  }

  // Test with all possible buttons (action, learn more, dismiss).
  {
    TestToolbarActionsBarBubbleDelegate delegate(
        HeadingString(), BodyString(), ActionString());
    delegate.set_dismiss_button_text(DismissString());
    delegate.set_learn_more_button_text(LearnMoreString());
    ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
    EXPECT_TRUE([bubble actionButton]);
    EXPECT_TRUE([bubble learnMoreButton]);
    EXPECT_TRUE([bubble dismissButton]);
    EXPECT_FALSE([bubble itemList]);

    [bubble close];
    chrome::testing::NSRunLoopRunAllPending();
  }

  // Test with only a dismiss button (no action button).
  {
    TestToolbarActionsBarBubbleDelegate delegate(
        HeadingString(), BodyString(), base::string16());
    delegate.set_dismiss_button_text(DismissString());
    ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
    EXPECT_FALSE([bubble actionButton]);
    EXPECT_FALSE([bubble learnMoreButton]);
    EXPECT_TRUE([bubble dismissButton]);
    EXPECT_FALSE([bubble itemList]);

    [bubble close];
    chrome::testing::NSRunLoopRunAllPending();
  }

  // Test with an action button and an item list.
  {
    TestToolbarActionsBarBubbleDelegate delegate(
        HeadingString(), BodyString(), ActionString());
    delegate.set_item_list_text(ItemListString());
    ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
    EXPECT_TRUE([bubble actionButton]);
    EXPECT_FALSE([bubble learnMoreButton]);
    EXPECT_FALSE([bubble dismissButton]);
    EXPECT_TRUE([bubble itemList]);

    [bubble close];
    chrome::testing::NSRunLoopRunAllPending();
  }

  // Test with all possible fields.
  {
    TestToolbarActionsBarBubbleDelegate delegate(
        HeadingString(), BodyString(), ActionString());
    delegate.set_dismiss_button_text(DismissString());
    delegate.set_learn_more_button_text(LearnMoreString());
    delegate.set_item_list_text(ItemListString());
    ToolbarActionsBarBubbleMac* bubble = CreateAndShowBubble(&delegate);
    EXPECT_TRUE([bubble actionButton]);
    EXPECT_TRUE([bubble learnMoreButton]);
    EXPECT_TRUE([bubble dismissButton]);
    EXPECT_TRUE([bubble itemList]);

    [bubble close];
    chrome::testing::NSRunLoopRunAllPending();
  }
}
