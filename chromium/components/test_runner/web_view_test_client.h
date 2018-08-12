// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TEST_RUNNER_WEB_VIEW_TEST_CLIENT_H_
#define COMPONENTS_TEST_RUNNER_WEB_VIEW_TEST_CLIENT_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "third_party/WebKit/public/web/WebViewClient.h"

namespace blink {
class WebView;
}  // namespace blink

namespace test_runner {

class EventSender;
class TestRunner;
class WebTestDelegate;
class WebTestProxyBase;

// WebViewTestClient implements WebViewClient interface, providing behavior
// expected by tests.  WebViewTestClient ends up used by WebTestProxy
// which coordinates forwarding WebViewClient calls either to
// WebViewTestClient or to the product code (i.e. to RenderViewImpl).
class WebViewTestClient : public blink::WebViewClient {
 public:
  // Caller has to ensure that all arguments (i.e. |test_runner| and |delegate|)
  // live longer than |this|.
  WebViewTestClient(TestRunner* test_runner,
                    WebTestDelegate* delegate,
                    EventSender* event_sender,
                    WebTestProxyBase* web_test_proxy_base);

  virtual ~WebViewTestClient();

  // WebViewClient overrides needed by WebTestProxy.
  void showValidationMessage(const blink::WebRect& anchor_in_root_view,
                             const blink::WebString& main_message,
                             blink::WebTextDirection main_message_hint,
                             const blink::WebString& sub_message,
                             blink::WebTextDirection sub_message_hint) override;
  bool runFileChooser(const blink::WebFileChooserParams& params,
                      blink::WebFileChooserCompletion* completion) override;
  void scheduleAnimation() override;
  void startDragging(blink::WebLocalFrame* frame,
                     const blink::WebDragData& data,
                     blink::WebDragOperationsMask mask,
                     const blink::WebImage& image,
                     const blink::WebPoint& point) override;
  void didChangeContents() override;
  blink::WebView* createView(blink::WebLocalFrame* creator,
                             const blink::WebURLRequest& request,
                             const blink::WebWindowFeatures& features,
                             const blink::WebString& frame_name,
                             blink::WebNavigationPolicy policy,
                             bool suppress_opener) override;
  void setStatusText(const blink::WebString& text) override;
  void printPage(blink::WebLocalFrame* frame) override;
  blink::WebSpeechRecognizer* speechRecognizer() override;
  bool requestPointerLock() override;
  void requestPointerUnlock() override;
  bool isPointerLocked() override;
  void didFocus() override;
  void setToolTipText(const blink::WebString& text,
                      blink::WebTextDirection direction) override;
  void resetInputMethod() override;
  blink::WebString acceptLanguages() override;

 private:
  void AnimateNow();

  // Borrowed pointers to other parts of Layout Tests state.
  TestRunner* test_runner_;
  WebTestDelegate* delegate_;
  EventSender* event_sender_;
  WebTestProxyBase* web_test_proxy_base_;

  bool animation_scheduled_;

  base::WeakPtrFactory<WebViewTestClient> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WebViewTestClient);
};

}  // namespace test_runner

#endif  // COMPONENTS_TEST_RUNNER_WEB_VIEW_TEST_CLIENT_H_
