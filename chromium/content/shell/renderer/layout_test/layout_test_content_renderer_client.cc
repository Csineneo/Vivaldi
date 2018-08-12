// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/renderer/layout_test/layout_test_content_renderer_client.h"

#include "base/callback.h"
#include "base/command_line.h"
#include "base/debug/debugger.h"
#include "components/test_runner/mock_credential_manager_client.h"
#include "components/test_runner/web_frame_test_proxy.h"
#include "components/test_runner/web_test_interfaces.h"
#include "components/test_runner/web_test_proxy.h"
#include "components/test_runner/web_test_runner.h"
#include "components/web_cache/renderer/web_cache_render_process_observer.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/renderer/render_view.h"
#include "content/public/test/layouttest_support.h"
#include "content/shell/common/shell_switches.h"
#include "content/shell/renderer/layout_test/blink_test_helpers.h"
#include "content/shell/renderer/layout_test/blink_test_runner.h"
#include "content/shell/renderer/layout_test/layout_test_render_frame_observer.h"
#include "content/shell/renderer/layout_test/layout_test_render_process_observer.h"
#include "content/shell/renderer/layout_test/test_media_stream_renderer_factory.h"
#include "content/shell/renderer/shell_render_view_observer.h"
#include "content/test/mock_webclipboard_impl.h"
#include "ppapi/shared_impl/ppapi_switches.h"
#include "third_party/WebKit/public/platform/WebMediaStreamCenter.h"
#include "third_party/WebKit/public/platform/modules/app_banner/WebAppBannerClient.h"
#include "third_party/WebKit/public/web/WebPluginParams.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "v8/include/v8.h"

using blink::WebAudioDevice;
using blink::WebClipboard;
using blink::WebLocalFrame;
using blink::WebMIDIAccessor;
using blink::WebMIDIAccessorClient;
using blink::WebMediaStreamCenter;
using blink::WebMediaStreamCenterClient;
using blink::WebPlugin;
using blink::WebPluginParams;
using blink::WebRTCPeerConnectionHandler;
using blink::WebRTCPeerConnectionHandlerClient;
using blink::WebThemeEngine;

namespace content {

namespace {

void WebTestProxyCreated(RenderView* render_view,
                         test_runner::WebTestProxyBase* proxy) {
  BlinkTestRunner* test_runner = new BlinkTestRunner(render_view);
  test_runner->set_proxy(proxy);
  if (!LayoutTestRenderProcessObserver::GetInstance()->test_delegate()) {
    LayoutTestRenderProcessObserver::GetInstance()->SetTestDelegate(
        test_runner);
  }
  proxy->set_view_test_client(LayoutTestRenderProcessObserver::GetInstance()
                                  ->test_interfaces()
                                  ->CreateWebViewTestClient(proxy));
  proxy->SetInterfaces(
      LayoutTestRenderProcessObserver::GetInstance()->test_interfaces());
  test_runner->proxy()->SetDelegate(
      LayoutTestRenderProcessObserver::GetInstance()->test_delegate());
}

void WebFrameTestProxyCreated(RenderFrame* render_frame,
                              test_runner::WebFrameTestProxyBase* proxy) {
  proxy->set_test_client(LayoutTestRenderProcessObserver::GetInstance()
                             ->test_interfaces()
                             ->CreateWebFrameTestClient());
}

}  // namespace

LayoutTestContentRendererClient::LayoutTestContentRendererClient() {
  EnableWebTestProxyCreation(base::Bind(&WebTestProxyCreated),
                             base::Bind(&WebFrameTestProxyCreated));
}

LayoutTestContentRendererClient::~LayoutTestContentRendererClient() {
}

void LayoutTestContentRendererClient::RenderThreadStarted() {
  ShellContentRendererClient::RenderThreadStarted();
  shell_observer_.reset(new LayoutTestRenderProcessObserver());
}

void LayoutTestContentRendererClient::RenderFrameCreated(
    RenderFrame* render_frame) {
  new LayoutTestRenderFrameObserver(render_frame);
}

void LayoutTestContentRendererClient::RenderViewCreated(
    RenderView* render_view) {
  new ShellRenderViewObserver(render_view);

  test_runner::WebTestProxyBase* proxy = GetWebTestProxyBase(render_view);
  proxy->set_web_widget(render_view->GetWebView());
  proxy->set_web_view(render_view->GetWebView());

  BlinkTestRunner* test_runner = BlinkTestRunner::Get(render_view);
  test_runner->Reset(false /* for_new_test */);

  LayoutTestRenderProcessObserver::GetInstance()
      ->test_interfaces()
      ->TestRunner()
      ->InitializeWebViewWithMocks(render_view->GetWebView());

  test_runner::WebTestDelegate* delegate =
      LayoutTestRenderProcessObserver::GetInstance()->test_delegate();
  if (delegate == static_cast<test_runner::WebTestDelegate*>(test_runner))
    LayoutTestRenderProcessObserver::GetInstance()->SetMainWindow(render_view);
}

WebMediaStreamCenter*
LayoutTestContentRendererClient::OverrideCreateWebMediaStreamCenter(
    WebMediaStreamCenterClient* client) {
#if defined(ENABLE_WEBRTC)
  test_runner::WebTestInterfaces* interfaces =
      LayoutTestRenderProcessObserver::GetInstance()->test_interfaces();
  return interfaces->CreateMediaStreamCenter(client);
#else
  return NULL;
#endif
}

WebRTCPeerConnectionHandler*
LayoutTestContentRendererClient::OverrideCreateWebRTCPeerConnectionHandler(
    WebRTCPeerConnectionHandlerClient* client) {
#if defined(ENABLE_WEBRTC)
  test_runner::WebTestInterfaces* interfaces =
      LayoutTestRenderProcessObserver::GetInstance()->test_interfaces();
  return interfaces->CreateWebRTCPeerConnectionHandler(client);
#else
  return NULL;
#endif
}

WebMIDIAccessor*
LayoutTestContentRendererClient::OverrideCreateMIDIAccessor(
    WebMIDIAccessorClient* client) {
  test_runner::WebTestInterfaces* interfaces =
      LayoutTestRenderProcessObserver::GetInstance()->test_interfaces();
  return interfaces->CreateMIDIAccessor(client);
}

WebAudioDevice*
LayoutTestContentRendererClient::OverrideCreateAudioDevice(
    double sample_rate) {
  test_runner::WebTestInterfaces* interfaces =
      LayoutTestRenderProcessObserver::GetInstance()->test_interfaces();
  return interfaces->CreateAudioDevice(sample_rate);
}

WebClipboard* LayoutTestContentRendererClient::OverrideWebClipboard() {
  if (!clipboard_)
    clipboard_.reset(new MockWebClipboardImpl);
  return clipboard_.get();
}

WebThemeEngine* LayoutTestContentRendererClient::OverrideThemeEngine() {
  return LayoutTestRenderProcessObserver::GetInstance()
      ->test_interfaces()
      ->ThemeEngine();
}

scoped_ptr<blink::WebAppBannerClient>
LayoutTestContentRendererClient::CreateAppBannerClient(
    RenderFrame* render_frame) {
  test_runner::WebTestInterfaces* interfaces =
      LayoutTestRenderProcessObserver::GetInstance()->test_interfaces();
  return interfaces->CreateAppBannerClient();
}

scoped_ptr<MediaStreamRendererFactory>
LayoutTestContentRendererClient::CreateMediaStreamRendererFactory() {
#if defined(ENABLE_WEBRTC)
  return scoped_ptr<MediaStreamRendererFactory>(
      new TestMediaStreamRendererFactory());
#else
  return nullptr;
#endif
}

}  // namespace content
