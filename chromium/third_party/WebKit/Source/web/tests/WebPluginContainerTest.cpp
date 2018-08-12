/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
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

#include "public/web/WebPluginContainer.h"

#include "core/dom/Element.h"
#include "core/events/KeyboardEvent.h"
#include "platform/PlatformEvent.h"
#include "platform/PlatformKeyboardEvent.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebClipboard.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebUnitTestSupport.h"
#include "public/web/WebDocument.h"
#include "public/web/WebElement.h"
#include "public/web/WebFrame.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebPluginParams.h"
#include "public/web/WebPrintParams.h"
#include "public/web/WebSettings.h"
#include "public/web/WebView.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebPluginContainerImpl.h"
#include "web/WebViewImpl.h"
#include "web/tests/FakeWebPlugin.h"
#include "web/tests/FrameTestHelpers.h"

using blink::testing::runPendingTasks;

namespace blink {

class WebPluginContainerTest : public ::testing::Test {
public:
    WebPluginContainerTest()
        : m_baseURL("http://www.test.com/")
    {
    }

    void TearDown() override
    {
        Platform::current()->unitTestSupport()->unregisterAllMockedURLs();
    }

    void calculateGeometry(WebPluginContainerImpl* pluginContainerImpl, IntRect& windowRect, IntRect& clipRect, IntRect& unobscuredRect, Vector<IntRect>& cutOutRects)
    {
        pluginContainerImpl->calculateGeometry(windowRect, clipRect, unobscuredRect, cutOutRects);
    }

protected:
    std::string m_baseURL;
};

class TestPluginWebFrameClient;

// Subclass of FakeWebPlugin that has a selection of 'x' as plain text and 'y' as markup text.
class TestPlugin : public FakeWebPlugin {
public:
    TestPlugin(WebFrame* frame, const WebPluginParams& params, TestPluginWebFrameClient* testClient)
        : FakeWebPlugin(frame, params)
    {
        m_testClient = testClient;
    }

    bool hasSelection() const override { return true; }
    WebString selectionAsText() const override { return WebString("x"); }
    WebString selectionAsMarkup() const override { return WebString("y"); }
    bool supportsPaginatedPrint() override { return true; }
    int printBegin(const WebPrintParams& printParams) override { return 1; }
    void printPage(int pageNumber, WebCanvas*) override;
private:
    TestPluginWebFrameClient* m_testClient;
};

class TestPluginWebFrameClient : public FrameTestHelpers::TestWebFrameClient {
    WebPlugin* createPlugin(WebLocalFrame* frame, const WebPluginParams& params) override
    {
        if (params.mimeType == "application/x-webkit-test-webplugin"
            || params.mimeType == "application/pdf")
            return new TestPlugin(frame, params, this);
        return WebFrameClient::createPlugin(frame, params);
    }

public:
    void onPrintPage() { m_printedPage = true; }
    bool printedAtLeastOnePage() { return m_printedPage; }

private:
    bool m_printedPage = false;
};

void TestPlugin::printPage(int pageNumber, WebCanvas* canvas)
{
    ASSERT(m_testClient);
    m_testClient->onPrintPage();
}

WebPluginContainer* getWebPluginContainer(WebView* webView, const WebString& id)
{
    WebElement element = webView->mainFrame()->document().getElementById(id);
    return element.pluginContainer();
}

TEST_F(WebPluginContainerTest, WindowToLocalPointTest)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_container.html"));
    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    WebPluginContainer* pluginContainerOne = getWebPluginContainer(webView, WebString::fromUTF8("translated-plugin"));
    ASSERT(pluginContainerOne);
    WebPoint point1 = pluginContainerOne->rootFrameToLocalPoint(WebPoint(10, 10));
    ASSERT_EQ(0, point1.x);
    ASSERT_EQ(0, point1.y);
    WebPoint point2 = pluginContainerOne->rootFrameToLocalPoint(WebPoint(100, 100));
    ASSERT_EQ(90, point2.x);
    ASSERT_EQ(90, point2.y);

    WebPluginContainer* pluginContainerTwo = getWebPluginContainer(webView, WebString::fromUTF8("rotated-plugin"));
    ASSERT(pluginContainerTwo);
    WebPoint point3 = pluginContainerTwo->rootFrameToLocalPoint(WebPoint(0, 10));
    ASSERT_EQ(10, point3.x);
    ASSERT_EQ(0, point3.y);
    WebPoint point4 = pluginContainerTwo->rootFrameToLocalPoint(WebPoint(-10, 10));
    ASSERT_EQ(10, point4.x);
    ASSERT_EQ(10, point4.y);
}

TEST_F(WebPluginContainerTest, PluginDocumentPluginIsFocused)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("test.pdf"), WebString::fromUTF8("application/pdf"));

    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "test.pdf", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->updateAllLifecyclePhases();

    WebDocument document = webView->mainFrame()->document();
    EXPECT_TRUE(document.isPluginDocument());
    WebPluginContainer* pluginContainer = getWebPluginContainer(webView, "plugin");
    EXPECT_EQ(document.focusedElement(), pluginContainer->element());
}

TEST_F(WebPluginContainerTest, IFramePluginDocumentNotFocused)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("test.pdf"), WebString::fromUTF8("application/pdf"));
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("iframe_pdf.html"), WebString::fromUTF8("text/html"));

    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "iframe_pdf.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->updateAllLifecyclePhases();

    WebDocument document = webView->mainFrame()->document();
    WebFrame* iframe = webView->mainFrame()->firstChild();
    EXPECT_TRUE(iframe->document().isPluginDocument());
    WebPluginContainer* pluginContainer = iframe->document().getElementById("plugin").pluginContainer();
    EXPECT_NE(document.focusedElement(), pluginContainer->element());
    EXPECT_NE(iframe->document().focusedElement(), pluginContainer->element());
}

TEST_F(WebPluginContainerTest, PrintOnePage)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("test.pdf"), WebString::fromUTF8("application/pdf"));

    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "test.pdf", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->updateAllLifecyclePhases();
    runPendingTasks();
    WebFrame* frame = webView->mainFrame();

    WebPrintParams printParams;
    printParams.printContentArea.width = 500;
    printParams.printContentArea.height = 500;

    frame->printBegin(printParams);
    SkPictureRecorder recorder;
    frame->printPage(0, recorder.beginRecording(IntRect()));
    frame->printEnd();
    ASSERT(pluginWebFrameClient.printedAtLeastOnePage());
}

TEST_F(WebPluginContainerTest, PrintAllPages)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("test.pdf"), WebString::fromUTF8("application/pdf"));

    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "test.pdf", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->updateAllLifecyclePhases();
    runPendingTasks();
    WebFrame* frame = webView->mainFrame();

    WebPrintParams printParams;
    printParams.printContentArea.width = 500;
    printParams.printContentArea.height = 500;

    frame->printBegin(printParams);
    SkPictureRecorder recorder;
    frame->printPagesWithBoundaries(recorder.beginRecording(IntRect()), WebSize());
    frame->printEnd();
    ASSERT(pluginWebFrameClient.printedAtLeastOnePage());
}

TEST_F(WebPluginContainerTest, LocalToWindowPointTest)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_container.html"));
    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    WebPluginContainer* pluginContainerOne = getWebPluginContainer(webView, WebString::fromUTF8("translated-plugin"));
    ASSERT(pluginContainerOne);
    WebPoint point1 = pluginContainerOne->localToRootFramePoint(WebPoint(0, 0));
    ASSERT_EQ(10, point1.x);
    ASSERT_EQ(10, point1.y);
    WebPoint point2 = pluginContainerOne->localToRootFramePoint(WebPoint(90, 90));
    ASSERT_EQ(100, point2.x);
    ASSERT_EQ(100, point2.y);

    WebPluginContainer* pluginContainerTwo = getWebPluginContainer(webView, WebString::fromUTF8("rotated-plugin"));
    ASSERT(pluginContainerTwo);
    WebPoint point3 = pluginContainerTwo->localToRootFramePoint(WebPoint(10, 0));
    ASSERT_EQ(0, point3.x);
    ASSERT_EQ(10, point3.y);
    WebPoint point4 = pluginContainerTwo->localToRootFramePoint(WebPoint(10, 10));
    ASSERT_EQ(-10, point4.x);
    ASSERT_EQ(10, point4.y);
}

// Verifies executing the command 'Copy' results in copying to the clipboard.
TEST_F(WebPluginContainerTest, Copy)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_container.html"));
    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    WebElement pluginContainerOneElement = webView->mainFrame()->document().getElementById(WebString::fromUTF8("translated-plugin"));
    EXPECT_TRUE(webView->mainFrame()->executeCommand("Copy",  pluginContainerOneElement));
    EXPECT_EQ(WebString("x"), Platform::current()->clipboard()->readPlainText(WebClipboard::Buffer()));
}

// Verifies |Ctrl-C| and |Ctrl-Insert| keyboard events, results in copying to
// the clipboard.
TEST_F(WebPluginContainerTest, CopyInsertKeyboardEventsTest)
{
    URLTestHelpers::registerMockedURLFromBaseURL(
        WebString::fromUTF8(m_baseURL.c_str()),
        WebString::fromUTF8("plugin_container.html"));
    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    WebElement pluginContainerOneElement = webView->mainFrame()->document().getElementById(WebString::fromUTF8("translated-plugin"));
    PlatformEvent::Modifiers modifierKey = static_cast<PlatformEvent::Modifiers>(PlatformEvent::CtrlKey | PlatformEvent::NumLockOn | PlatformEvent::IsLeft);
#if OS(MACOSX)
    modifierKey = static_cast<PlatformEvent::Modifiers>(PlatformEvent::MetaKey | PlatformEvent::NumLockOn | PlatformEvent::IsLeft);
#endif
    PlatformKeyboardEvent platformKeyboardEventC(PlatformEvent::RawKeyDown, "", "", "67", "", "", 67, 0, false, modifierKey, 0.0);
    RefPtrWillBeRawPtr<KeyboardEvent> keyEventC = KeyboardEvent::create(platformKeyboardEventC, 0);
    toWebPluginContainerImpl(pluginContainerOneElement.pluginContainer())->handleEvent(keyEventC.get());
    EXPECT_EQ(WebString("x"), Platform::current()->clipboard()->readPlainText(WebClipboard::Buffer()));

    // Clearing |Clipboard::Buffer()|.
    Platform::current()->clipboard()->writePlainText(WebString(""));
    EXPECT_EQ(WebString(""), Platform::current()->clipboard()->readPlainText(WebClipboard::Buffer()));

    PlatformKeyboardEvent platformKeyboardEventInsert(PlatformEvent::RawKeyDown, "", "", "45", "", "", 45, 0, false, modifierKey, 0.0);
    RefPtrWillBeRawPtr<KeyboardEvent> keyEventInsert = KeyboardEvent::create(platformKeyboardEventInsert, 0);
    toWebPluginContainerImpl(pluginContainerOneElement.pluginContainer())->handleEvent(keyEventInsert.get());
    EXPECT_EQ(WebString("x"), Platform::current()->clipboard()->readPlainText(WebClipboard::Buffer()));
}

// A class to facilitate testing that events are correctly received by plugins.
class EventTestPlugin : public FakeWebPlugin {
public:
    EventTestPlugin(WebFrame* frame, const WebPluginParams& params)
        : FakeWebPlugin(frame, params)
        , m_lastEventType(WebInputEvent::Undefined)
    {
    }

    WebInputEventResult handleInputEvent(const WebInputEvent& event, WebCursorInfo&) override
    {
        m_lastEventType = event.type;
        return WebInputEventResult::HandledSystem;
    }
    WebInputEvent::Type getLastInputEventType() {return m_lastEventType; }

private:
    WebInputEvent::Type m_lastEventType;
};

class EventTestPluginWebFrameClient : public FrameTestHelpers::TestWebFrameClient {
    WebPlugin* createPlugin(WebLocalFrame* frame, const WebPluginParams& params) override
    {
        if (params.mimeType == WebString::fromUTF8("application/x-webkit-test-webplugin"))
            return new EventTestPlugin(frame, params);
        return WebFrameClient::createPlugin(frame, params);
    }
};

TEST_F(WebPluginContainerTest, GestureLongPressReachesPlugin)
{
    URLTestHelpers::registerMockedURLFromBaseURL(
        WebString::fromUTF8(m_baseURL.c_str()),
        WebString::fromUTF8("plugin_container.html"));
    EventTestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    WebElement pluginContainerOneElement = webView->mainFrame()->document().getElementById(WebString::fromUTF8("translated-plugin"));
    WebPlugin* plugin = static_cast<WebPluginContainerImpl*>(pluginContainerOneElement.pluginContainer())->plugin();
    EventTestPlugin* testPlugin = static_cast<EventTestPlugin*>(plugin);

    WebGestureEvent event;
    event.type = WebInputEvent::GestureLongPress;
    event.sourceDevice = WebGestureDeviceTouchscreen;

    // First, send an event that doesn't hit the plugin to verify that the
    // plugin doesn't receive it.
    event.x = 0;
    event.y = 0;

    webView->handleInputEvent(event);
    runPendingTasks();

    EXPECT_EQ(WebInputEvent::Undefined, testPlugin->getLastInputEventType());

    // Next, send an event that does hit the plugin, and verify it does receive it.
    WebRect rect = pluginContainerOneElement.boundsInViewport();
    event.x = rect.x + rect.width / 2;
    event.y = rect.y + rect.height / 2;

    webView->handleInputEvent(event);
    runPendingTasks();

    EXPECT_EQ(WebInputEvent::GestureLongPress, testPlugin->getLastInputEventType());
}

// Verify that isRectTopmost returns false when the document is detached.
TEST_F(WebPluginContainerTest, IsRectTopmostTest)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_container.html"));
    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    RefPtrWillBeRawPtr<WebPluginContainerImpl> pluginContainerImpl =
        toWebPluginContainerImpl(getWebPluginContainer(webView, WebString::fromUTF8("translated-plugin")));
    pluginContainerImpl->setFrameRect(IntRect(0, 0, 300, 300));

    WebRect rect = pluginContainerImpl->element().boundsInViewport();
    EXPECT_TRUE(pluginContainerImpl->isRectTopmost(rect));

    // Cause the plugin's frame to be detached.
    webViewHelper.reset();

    EXPECT_FALSE(pluginContainerImpl->isRectTopmost(rect));
}

#define EXPECT_RECT_EQ(expected, actual) \
    do { \
        const IntRect& actualRect = actual; \
        EXPECT_EQ(expected.x(), actualRect.x()); \
        EXPECT_EQ(expected.y(), actualRect.y()); \
        EXPECT_EQ(expected.width(), actualRect.width()); \
        EXPECT_EQ(expected.height(), actualRect.height()); \
    } while (false)

TEST_F(WebPluginContainerTest, ClippedRectsForIframedElement)
{
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_container.html"));
    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_containing_page.html"));

    TestPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_containing_page.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    WebElement pluginElement = webView->mainFrame()->firstChild()->document().getElementById("translated-plugin");
    RefPtrWillBeRawPtr<WebPluginContainerImpl> pluginContainerImpl = toWebPluginContainerImpl(pluginElement.pluginContainer());

    ASSERT(pluginContainerImpl.get());
    pluginContainerImpl->setFrameRect(IntRect(0, 0, 300, 300));

    IntRect windowRect, clipRect, unobscuredRect;
    Vector<IntRect> cutOutRects;
    calculateGeometry(pluginContainerImpl.get(), windowRect, clipRect, unobscuredRect, cutOutRects);
    EXPECT_RECT_EQ(IntRect(10, 210, 300, 300), windowRect);
    EXPECT_RECT_EQ(IntRect(0, 0, 240, 90), clipRect);
    EXPECT_RECT_EQ(IntRect(0, 0, 240, 160), unobscuredRect);

    // Cause the plugin's frame to be detached.
    webViewHelper.reset();
}

TEST_F(WebPluginContainerTest, TopmostAfterDetachTest)
{
    static WebRect topmostRect(10, 10, 40, 40);

    // Plugin that checks isRectTopmost in destroy().
    class TopmostPlugin : public FakeWebPlugin {
    public:
        TopmostPlugin(WebFrame* frame, const WebPluginParams& params)
            : FakeWebPlugin(frame, params) {}

        bool isRectTopmost()
        {
            return container()->isRectTopmost(topmostRect);
        }

        void destroy() override
        {
            // In destroy, isRectTopmost is no longer valid.
            EXPECT_FALSE(container()->isRectTopmost(topmostRect));
            FakeWebPlugin::destroy();
        }
    };

    class TopmostPluginWebFrameClient : public FrameTestHelpers::TestWebFrameClient {
        WebPlugin* createPlugin(WebLocalFrame* frame, const WebPluginParams& params) override
        {
            return new TopmostPlugin(frame, params);
        }
    };

    URLTestHelpers::registerMockedURLFromBaseURL(WebString::fromUTF8(m_baseURL.c_str()), WebString::fromUTF8("plugin_container.html"));
    TopmostPluginWebFrameClient pluginWebFrameClient; // Must outlive webViewHelper.
    FrameTestHelpers::WebViewHelper webViewHelper;
    WebView* webView = webViewHelper.initializeAndLoad(m_baseURL + "plugin_container.html", true, &pluginWebFrameClient);
    ASSERT(webView);
    webView->settings()->setPluginsEnabled(true);
    webView->resize(WebSize(300, 300));
    webView->updateAllLifecyclePhases();
    runPendingTasks();

    RefPtrWillBeRawPtr<WebPluginContainerImpl> pluginContainerImpl =
        toWebPluginContainerImpl(getWebPluginContainer(webView, WebString::fromUTF8("translated-plugin")));
    pluginContainerImpl->setFrameRect(IntRect(0, 0, 300, 300));

    EXPECT_TRUE(pluginContainerImpl->isRectTopmost(topmostRect));

    TopmostPlugin* testPlugin = static_cast<TopmostPlugin*>(pluginContainerImpl->plugin());
    EXPECT_TRUE(testPlugin->isRectTopmost());

    // Cause the plugin's frame to be detached.
    webViewHelper.reset();

    EXPECT_FALSE(pluginContainerImpl->isRectTopmost(topmostRect));
}

} // namespace blink
