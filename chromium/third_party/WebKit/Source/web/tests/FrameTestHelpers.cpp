/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "web/tests/FrameTestHelpers.h"

#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebData.h"
#include "public/platform/WebString.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/WebURLResponse.h"
#include "public/platform/WebUnitTestSupport.h"
#include "public/web/WebFrameWidget.h"
#include "public/web/WebRemoteFrame.h"
#include "public/web/WebSettings.h"
#include "public/web/WebTreeScopeType.h"
#include "public/web/WebViewClient.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebRemoteFrameImpl.h"
#include "wtf/Functional.h"
#include "wtf/StdLibExtras.h"
#include "wtf/text/StringBuilder.h"

namespace blink {
namespace FrameTestHelpers {

namespace {

// The frame test helpers coordinate frame loads in a carefully choreographed
// dance. Since the parser is threaded, simply spinning the run loop once is not
// enough to ensure completion of a load. Instead, the following pattern is
// used to ensure that tests see the final state:
// 1. Starts a load.
// 2. Enter the run loop.
// 3. Posted task triggers the load, and starts pumping pending resource
//    requests using runServeAsyncRequestsTask().
// 4. TestWebFrameClient watches for didStartLoading/didStopLoading calls,
//    keeping track of how many loads it thinks are in flight.
// 5. While runServeAsyncRequestsTask() observes TestWebFrameClient to still
//    have loads in progress, it posts itself back to the run loop.
// 6. When runServeAsyncRequestsTask() notices there are no more loads in
//    progress, it exits the run loop.
// 7. At this point, all parsing, resource loads, and layout should be finished.
TestWebFrameClient* testClientForFrame(WebFrame* frame)
{
    return static_cast<TestWebFrameClient*>(toWebLocalFrameImpl(frame)->client());
}

void runServeAsyncRequestsTask(TestWebFrameClient* client)
{
    Platform::current()->unitTestSupport()->serveAsynchronousMockedRequests();
    if (client->isLoading())
        Platform::current()->currentThread()->taskRunner()->postTask(BLINK_FROM_HERE, bind(&runServeAsyncRequestsTask, client));
    else
        testing::exitRunLoop();
}

void pumpPendingRequests(WebFrame* frame)
{
    Platform::current()->currentThread()->taskRunner()->postTask(BLINK_FROM_HERE, bind(&runServeAsyncRequestsTask, testClientForFrame(frame)));
    testing::enterRunLoop();
}

TestWebFrameClient* defaultWebFrameClient()
{
    DEFINE_STATIC_LOCAL(TestWebFrameClient, client, ());
    return &client;
}

TestWebViewClient* defaultWebViewClient()
{
    DEFINE_STATIC_LOCAL(TestWebViewClient,  client, ());
    return &client;
}

} // namespace

void loadFrame(WebFrame* frame, const std::string& url)
{
    WebURLRequest urlRequest;
    urlRequest.initialize();
    urlRequest.setURL(URLTestHelpers::toKURL(url));
    frame->loadRequest(urlRequest);
    pumpPendingRequests(frame);
}

void loadHTMLString(WebFrame* frame, const std::string& html, const WebURL& baseURL)
{
    frame->loadHTMLString(WebData(html.data(), html.size()), baseURL);
    pumpPendingRequests(frame);
}

void loadHistoryItem(WebFrame* frame, const WebHistoryItem& item, WebHistoryLoadType loadType, WebURLRequest::CachePolicy cachePolicy)
{
    frame->loadHistoryItem(item, loadType, cachePolicy);
    pumpPendingRequests(frame);
}

void reloadFrame(WebFrame* frame)
{
    frame->reload(false);
    pumpPendingRequests(frame);
}

void reloadFrameIgnoringCache(WebFrame* frame)
{
    frame->reload(true);
    pumpPendingRequests(frame);
}

void pumpPendingRequestsDoNotUse(WebFrame* frame)
{
    pumpPendingRequests(frame);
}

WebLocalFrame* createLocalChild(WebRemoteFrame* parent, const WebString& name, WebFrameClient* client, WebFrame* previousSibling, const WebFrameOwnerProperties& properties)
{
    if (!client)
        client = defaultWebFrameClient();

    // |uniqueName| is normally calculated in a somewhat complicated way by the
    // FrameTree class, but for test purposes the approximation below should be
    // close enough.
    static int uniqueNameCounter = 0;
    StringBuilder uniqueName;
    uniqueName.append(name);
    uniqueName.appendNumber(uniqueNameCounter++);

    return parent->createLocalChild(WebTreeScopeType::Document, name, uniqueName.toString(), WebSandboxFlags::None, client, previousSibling, properties);
}

WebViewHelper::WebViewHelper(SettingOverrider* settingOverrider)
    : m_webView(nullptr)
    , m_webViewWidget(nullptr)
    , m_settingOverrider(settingOverrider)
{
}

WebViewHelper::~WebViewHelper()
{
    reset();
}

WebViewImpl* WebViewHelper::initialize(bool enableJavascript, TestWebFrameClient* webFrameClient, TestWebViewClient* webViewClient, void (*updateSettingsFunc)(WebSettings*))
{
    reset();

    if (!webFrameClient)
        webFrameClient = defaultWebFrameClient();
    if (!webViewClient)
        webViewClient = defaultWebViewClient();
    m_webView = WebViewImpl::create(webViewClient);
    m_webView->settings()->setJavaScriptEnabled(enableJavascript);
    m_webView->settings()->setPluginsEnabled(true);
    // Enable (mocked) network loads of image URLs, as this simplifies
    // the completion of resource loads upon test shutdown & helps avoid
    // dormant loads trigger Resource leaks for image loads.
    //
    // Consequently, all external image resources must be mocked.
    m_webView->settings()->setLoadsImagesAutomatically(true);
    if (updateSettingsFunc)
        updateSettingsFunc(m_webView->settings());
    else
        m_webView->settings()->setDeviceSupportsMouse(false);
    if (m_settingOverrider)
        m_settingOverrider->overrideSettings(m_webView->settings());
    m_webView->setDeviceScaleFactor(webViewClient->screenInfo().deviceScaleFactor);
    m_webView->setDefaultPageScaleLimits(1, 4);
    WebLocalFrame* frame = WebLocalFrameImpl::create(WebTreeScopeType::Document, webFrameClient);
    m_webView->setMainFrame(frame);
    // TODO(dcheng): The main frame widget currently has a special case.
    // Eliminate this once WebView is no longer a WebWidget.
    m_webViewWidget = blink::WebFrameWidget::create(webViewClient, m_webView, frame);

    m_testWebViewClient = webViewClient;

    return m_webView;
}

WebViewImpl* WebViewHelper::initializeAndLoad(const std::string& url, bool enableJavascript, TestWebFrameClient* webFrameClient, TestWebViewClient* webViewClient, void (*updateSettingsFunc)(WebSettings*))
{
    initialize(enableJavascript, webFrameClient, webViewClient, updateSettingsFunc);

    loadFrame(webView()->mainFrame(), url);

    return webViewImpl();
}

void WebViewHelper::reset()
{
    if (m_webViewWidget) {
        m_webViewWidget->close();
        m_webViewWidget = nullptr;
    }
    if (m_webView) {
        ASSERT(m_webView->mainFrame()->isWebRemoteFrame() || !testClientForFrame(m_webView->mainFrame())->isLoading());
        m_webView->willCloseLayerTreeView();
        m_webView->close();
        m_webView = nullptr;
    }
}

void WebViewHelper::resize(WebSize size)
{
    m_testWebViewClient->clearAnimationScheduled();
    webViewImpl()->resize(size);
    EXPECT_FALSE(m_testWebViewClient->animationScheduled());
    m_testWebViewClient->clearAnimationScheduled();
}

TestWebFrameClient::TestWebFrameClient() : m_loadsInProgress(0)
{
}

WebFrame* TestWebFrameClient::createChildFrame(WebLocalFrame* parent, WebTreeScopeType scope, const WebString& name, const WebString& uniqueName, WebSandboxFlags sandboxFlags, const WebFrameOwnerProperties& frameOwnerProperties)
{
    WebFrame* frame = WebLocalFrame::create(scope, this);
    parent->appendChild(frame);
    return frame;
}

void TestWebFrameClient::frameDetached(WebFrame* frame, DetachType type)
{
    if (type == DetachType::Remove && frame->parent())
        frame->parent()->removeChild(frame);
    frame->close();
}

void TestWebFrameClient::didStartLoading(bool)
{
    ++m_loadsInProgress;
}

void TestWebFrameClient::didStopLoading()
{
    ASSERT(m_loadsInProgress > 0);
    --m_loadsInProgress;
}

void TestWebFrameClient::waitForLoadToComplete()
{
    for (;;) {
        // We call runPendingTasks multiple times as single call of
        // runPendingTasks may not be enough.
        // runPendingTasks only ensures that main thread task queue is empty,
        // and asynchronous parsing make use of off main thread HTML parser.
        testing::runPendingTasks();
        if (!isLoading())
            break;

        testing::yieldCurrentThread();
    }
}

TestWebRemoteFrameClient::TestWebRemoteFrameClient()
    : m_frame(WebRemoteFrameImpl::create(WebTreeScopeType::Document, this))
{
}

void TestWebRemoteFrameClient::frameDetached(DetachType type)
{
    if (type == DetachType::Remove && m_frame->parent())
        m_frame->parent()->removeChild(m_frame);
    m_frame->close();
}

void TestWebViewClient::initializeLayerTreeView()
{
    m_layerTreeView = adoptPtr(Platform::current()->unitTestSupport()->createLayerTreeViewForTesting());
    ASSERT(m_layerTreeView);
}

} // namespace FrameTestHelpers
} // namespace blink
