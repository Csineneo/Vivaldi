// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/browser/renderer_host/input/synthetic_smooth_scroll_gesture.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/input/synthetic_web_input_event_builders.h"
#include "content/common/input_messages.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "ui/events/event_switches.h"
#include "ui/events/latency_info.h"

using blink::WebInputEvent;

namespace {

const char kNonBlockingEventDataURL[] =
    "data:text/html;charset=utf-8,"
    "<!DOCTYPE html>"
    "<meta name='viewport' content='width=device-width'/>"
    "<style>"
    "html, body {"
    "  margin: 0;"
    "}"
    ".spacer { height: 1000px; }"
    "</style>"
    "<div class=spacer></div>"
    "<script>"
    "  document.addEventListener('wheel', function(e) { while(true) {} }, "
    "{'passive': true});"
    "  document.addEventListener('touchstart', function(e) { while(true) {} }, "
    "{'passive': true});"
    "  document.title='ready';"
    "</script>";

}  // namespace

namespace content {

class NonBlockingEventBrowserTest : public ContentBrowserTest {
 public:
  NonBlockingEventBrowserTest() {}
  ~NonBlockingEventBrowserTest() override {}

  RenderWidgetHostImpl* GetWidgetHost() {
    return RenderWidgetHostImpl::From(
        shell()->web_contents()->GetRenderViewHost()->GetWidget());
  }

  void OnSyntheticGestureCompleted(SyntheticGesture::Result result) {
    EXPECT_EQ(SyntheticGesture::GESTURE_FINISHED, result);
  }

 protected:
  void LoadURL() {
    const GURL data_url(kNonBlockingEventDataURL);
    NavigateToURL(shell(), data_url);

    RenderWidgetHostImpl* host = GetWidgetHost();
    host->GetView()->SetSize(gfx::Size(400, 400));

    base::string16 ready_title(base::ASCIIToUTF16("ready"));
    TitleWatcher watcher(shell()->web_contents(), ready_title);
    ignore_result(watcher.WaitAndGetTitle());

    MainThreadFrameObserver main_thread_sync(host);
    main_thread_sync.Wait();
  }

  // ContentBrowserTest:
  void SetUpCommandLine(base::CommandLine* cmd) override {
    // TODO(dtapuska): Remove this switch once wheel-gestures ships.
    cmd->AppendSwitch(switches::kEnableExperimentalWebPlatformFeatures);
    cmd->AppendSwitch(switches::kEnableWheelGestures);
  }

  int ExecuteScriptAndExtractInt(const std::string& script) {
    int value = 0;
    EXPECT_TRUE(content::ExecuteScriptAndExtractInt(
        shell()->web_contents(), "domAutomationController.send(" + script + ")",
        &value));
    return value;
  }

  int GetScrollTop() {
    return ExecuteScriptAndExtractInt("document.scrollingElement.scrollTop");
  }

  void DoWheelScroll() {
    EXPECT_EQ(0, GetScrollTop());

    int scrollHeight =
        ExecuteScriptAndExtractInt("document.documentElement.scrollHeight");
    EXPECT_EQ(1000, scrollHeight);

    scoped_refptr<FrameWatcher> frame_watcher(new FrameWatcher());
    GetWidgetHost()->GetProcess()->AddFilter(frame_watcher.get());
    scoped_refptr<InputMsgWatcher> input_msg_watcher(
        new InputMsgWatcher(GetWidgetHost(), blink::WebInputEvent::MouseWheel));

    GetWidgetHost()->ForwardWheelEvent(
        SyntheticWebMouseWheelEventBuilder::Build(10, 10, 0, -53, 0, true));

    // Runs until we get the InputMsgAck callback
    EXPECT_EQ(INPUT_EVENT_ACK_STATE_SET_NON_BLOCKING,
              input_msg_watcher->WaitForAck());
    frame_watcher->WaitFrames(1);

    // Expect that the compositor scrolled at least one pixel while the
    // main thread was in a busy loop.
    EXPECT_LT(0, frame_watcher->LastMetadata().root_scroll_offset.y());
  }

  void DoTouchScroll() {
    EXPECT_EQ(0, GetScrollTop());

    int scrollHeight =
        ExecuteScriptAndExtractInt("document.documentElement.scrollHeight");
    EXPECT_EQ(1000, scrollHeight);

    scoped_refptr<FrameWatcher> frame_watcher(new FrameWatcher());
    GetWidgetHost()->GetProcess()->AddFilter(frame_watcher.get());

    SyntheticSmoothScrollGestureParams params;
    params.gesture_source_type = SyntheticGestureParams::TOUCH_INPUT;
    params.anchor = gfx::PointF(50, 50);
    params.distances.push_back(gfx::Vector2d(0, -45));

    scoped_ptr<SyntheticSmoothScrollGesture> gesture(
        new SyntheticSmoothScrollGesture(params));
    GetWidgetHost()->QueueSyntheticGesture(
        std::move(gesture),
        base::Bind(&NonBlockingEventBrowserTest::OnSyntheticGestureCompleted,
                   base::Unretained(this)));

    // Expect that the compositor scrolled at least one pixel while the
    // main thread was in a busy loop.
    while (frame_watcher->LastMetadata().root_scroll_offset.y() <= 0)
      frame_watcher->WaitFrames(1);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NonBlockingEventBrowserTest);
};

// Disabled on MacOS because it doesn't support wheel gestures
// just yet.
#if defined(OS_MACOSX)
#define MAYBE_MouseWheel DISABLED_MouseWheel
#else
// Also appears to be flaky under TSan. crbug.com/588199
#if defined(THREAD_SANITIZER)
#define MAYBE_MouseWheel DISABLED_MouseWheel
#else
#define MAYBE_MouseWheel MouseWheel
#endif
#endif
IN_PROC_BROWSER_TEST_F(NonBlockingEventBrowserTest, MAYBE_MouseWheel) {
  LoadURL();
  DoWheelScroll();
}

// Disabled on MacOS because it doesn't support touch input.
#if defined(OS_MACOSX)
#define MAYBE_TouchStart DISABLED_TouchStart
#else
#define MAYBE_TouchStart TouchStart
#endif
IN_PROC_BROWSER_TEST_F(NonBlockingEventBrowserTest, MAYBE_TouchStart) {
  LoadURL();
  DoTouchScroll();
}

}  // namespace content
