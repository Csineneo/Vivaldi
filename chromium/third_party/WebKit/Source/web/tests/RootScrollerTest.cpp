// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/FrameHost.h"
#include "core/frame/FrameView.h"
#include "core/frame/RootFrameViewport.h"
#include "core/frame/TopControls.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/Page.h"
#include "core/page/scrolling/RootScrollerController.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/PaintLayerScrollableArea.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include "public/web/WebCache.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebRemoteFrame.h"
#include "public/web/WebScriptSource.h"
#include "public/web/WebSettings.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebRemoteFrameImpl.h"
#include "web/tests/FrameTestHelpers.h"
#include "wtf/Vector.h"

using blink::testing::runPendingTasks;
using testing::Mock;

namespace blink {

namespace {

class RootScrollerTest : public ::testing::Test {
 public:
  RootScrollerTest() : m_baseURL("http://www.test.com/") {
    registerMockedHttpURLLoad("overflow-scrolling.html");
    registerMockedHttpURLLoad("root-scroller.html");
    registerMockedHttpURLLoad("root-scroller-iframe.html");
    registerMockedHttpURLLoad("root-scroller-child.html");
  }

  ~RootScrollerTest() override {
    m_featuresBackup.restore();
    Platform::current()->getURLLoaderMockFactory()->unregisterAllURLs();
    WebCache::clear();
  }

  WebViewImpl* initialize(const std::string& pageName,
                          FrameTestHelpers::TestWebViewClient* client) {
    RuntimeEnabledFeatures::setSetRootScrollerEnabled(true);

    m_helper.initializeAndLoad(m_baseURL + pageName, true, nullptr, client,
                               nullptr, &configureSettings);

    // Initialize top controls to be shown.
    webViewImpl()->resizeWithTopControls(IntSize(400, 400), 50, true);
    webViewImpl()->topControls().setShownRatio(1);

    mainFrameView()->updateAllLifecyclePhases();

    return webViewImpl();
  }

  WebViewImpl* initialize(const std::string& pageName) {
    return initialize(pageName, &m_client);
  }

  static void configureSettings(WebSettings* settings) {
    settings->setJavaScriptEnabled(true);
    settings->setAcceleratedCompositingEnabled(true);
    settings->setPreferCompositingToLCDTextEnabled(true);
    // Android settings.
    settings->setViewportEnabled(true);
    settings->setViewportMetaEnabled(true);
    settings->setShrinksViewportContentToFit(true);
    settings->setMainFrameResizesAreOrientationChanges(true);
  }

  void registerMockedHttpURLLoad(const std::string& fileName) {
    URLTestHelpers::registerMockedURLFromBaseURL(
        WebString::fromUTF8(m_baseURL.c_str()),
        WebString::fromUTF8(fileName.c_str()));
  }

  void executeScript(const WebString& code) {
    mainWebFrame()->executeScript(WebScriptSource(code));
    mainWebFrame()->view()->updateAllLifecyclePhases();
    runPendingTasks();
  }

  WebViewImpl* webViewImpl() const { return m_helper.webView(); }

  FrameHost& frameHost() const {
    return m_helper.webView()->page()->frameHost();
  }

  LocalFrame* mainFrame() const {
    return webViewImpl()->mainFrameImpl()->frame();
  }

  WebLocalFrame* mainWebFrame() const { return webViewImpl()->mainFrameImpl(); }

  FrameView* mainFrameView() const {
    return webViewImpl()->mainFrameImpl()->frame()->view();
  }

  VisualViewport& visualViewport() const {
    return frameHost().visualViewport();
  }

  TopControls& topControls() const { return frameHost().topControls(); }

  Element* effectiveRootScroller(Document* doc) const {
    return doc->rootScrollerController()->effectiveRootScroller();
  }

  WebGestureEvent generateTouchGestureEvent(WebInputEvent::Type type,
                                            int deltaX = 0,
                                            int deltaY = 0) {
    return generateGestureEvent(type, WebGestureDeviceTouchscreen, deltaX,
                                deltaY);
  }

  WebGestureEvent generateWheelGestureEvent(WebInputEvent::Type type,
                                            int deltaX = 0,
                                            int deltaY = 0) {
    return generateGestureEvent(type, WebGestureDeviceTouchpad, deltaX, deltaY);
  }

 protected:
  WebGestureEvent generateGestureEvent(WebInputEvent::Type type,
                                       WebGestureDevice device,
                                       int deltaX,
                                       int deltaY) {
    WebGestureEvent event;
    event.type = type;
    event.sourceDevice = device;
    event.x = 100;
    event.y = 100;
    if (type == WebInputEvent::GestureScrollUpdate) {
      event.data.scrollUpdate.deltaX = deltaX;
      event.data.scrollUpdate.deltaY = deltaY;
    }
    return event;
  }

  std::string m_baseURL;
  FrameTestHelpers::TestWebViewClient m_client;
  FrameTestHelpers::WebViewHelper m_helper;
  RuntimeEnabledFeatures::Backup m_featuresBackup;
};

// Test that no root scroller element is set if setRootScroller isn't called on
// any elements. The document element should be the default effective root
// scroller.
TEST_F(RootScrollerTest, TestDefaultRootScroller) {
  initialize("overflow-scrolling.html");

  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());

  Element* htmlElement = mainFrame()->document()->documentElement();
  EXPECT_EQ(htmlElement, effectiveRootScroller(mainFrame()->document()));
}

class OverscrollTestWebViewClient : public FrameTestHelpers::TestWebViewClient {
 public:
  MOCK_METHOD4(didOverscroll,
               void(const WebFloatSize&,
                    const WebFloatSize&,
                    const WebFloatPoint&,
                    const WebFloatSize&));
};

// Tests that setting an element as the root scroller causes it to control url
// bar hiding and overscroll.
TEST_F(RootScrollerTest, TestSetRootScroller) {
  OverscrollTestWebViewClient client;
  initialize("root-scroller.html", &client);

  Element* container = mainFrame()->document()->getElementById("container");
  TrackExceptionState exceptionState;
  mainFrame()->document()->setRootScroller(container, exceptionState);
  ASSERT_EQ(container, mainFrame()->document()->rootScroller());

  // Content is 1000x1000, WebView size is 400x400 so max scroll is 600px.
  double maximumScroll = 600;

  webViewImpl()->handleInputEvent(
      generateTouchGestureEvent(WebInputEvent::GestureScrollBegin));

  {
    // Scrolling over the #container DIV should cause the top controls to
    // hide.
    EXPECT_FLOAT_EQ(1, topControls().shownRatio());
    webViewImpl()->handleInputEvent(generateTouchGestureEvent(
        WebInputEvent::GestureScrollUpdate, 0, -topControls().height()));
    EXPECT_FLOAT_EQ(0, topControls().shownRatio());
  }

  {
    // Make sure we're actually scrolling the DIV and not the FrameView.
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollUpdate, 0, -100));
    EXPECT_FLOAT_EQ(100, container->scrollTop());
    EXPECT_FLOAT_EQ(0, mainFrameView()->scrollPositionDouble().y());
  }

  {
    // Scroll 50 pixels past the end. Ensure we report the 50 pixels as
    // overscroll.
    EXPECT_CALL(client, didOverscroll(WebFloatSize(0, 50), WebFloatSize(0, 50),
                                      WebFloatPoint(100, 100), WebFloatSize()));
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollUpdate, 0, -550));
    EXPECT_FLOAT_EQ(maximumScroll, container->scrollTop());
    EXPECT_FLOAT_EQ(0, mainFrameView()->scrollPositionDouble().y());
    Mock::VerifyAndClearExpectations(&client);
  }

  {
    // Continue the gesture overscroll.
    EXPECT_CALL(client, didOverscroll(WebFloatSize(0, 20), WebFloatSize(0, 70),
                                      WebFloatPoint(100, 100), WebFloatSize()));
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollUpdate, 0, -20));
    EXPECT_FLOAT_EQ(maximumScroll, container->scrollTop());
    EXPECT_FLOAT_EQ(0, mainFrameView()->scrollPositionDouble().y());
    Mock::VerifyAndClearExpectations(&client);
  }

  webViewImpl()->handleInputEvent(
      generateTouchGestureEvent(WebInputEvent::GestureScrollEnd));

  {
    // Make sure a new gesture scroll still won't scroll the frameview and
    // overscrolls.
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollBegin));

    EXPECT_CALL(client, didOverscroll(WebFloatSize(0, 30), WebFloatSize(0, 30),
                                      WebFloatPoint(100, 100), WebFloatSize()));
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollUpdate, 0, -30));
    EXPECT_FLOAT_EQ(maximumScroll, container->scrollTop());
    EXPECT_FLOAT_EQ(0, mainFrameView()->scrollPositionDouble().y());
    Mock::VerifyAndClearExpectations(&client);

    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollEnd));
  }

  {
    // Scrolling up should show the top controls.
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollBegin));

    EXPECT_FLOAT_EQ(0, topControls().shownRatio());
    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollUpdate, 0, 30));
    EXPECT_FLOAT_EQ(0.6, topControls().shownRatio());

    webViewImpl()->handleInputEvent(
        generateTouchGestureEvent(WebInputEvent::GestureScrollEnd));
  }

  // Reset manually to avoid lifetime issues with custom WebViewClient.
  m_helper.reset();
}

// Tests that removing the element that is the root scroller from the DOM tree
// doesn't remove it as the root scroller but it does change the effective root
// scroller.
TEST_F(RootScrollerTest, TestRemoveRootScrollerFromDom) {
  initialize("root-scroller.html");

  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());

  Element* container = mainFrame()->document()->getElementById("container");
  TrackExceptionState exceptionState;
  mainFrame()->document()->setRootScroller(container, exceptionState);

  EXPECT_EQ(container, mainFrame()->document()->rootScroller());
  EXPECT_EQ(container, effectiveRootScroller(mainFrame()->document()));

  mainFrame()->document()->body()->removeChild(container);
  mainFrameView()->updateAllLifecyclePhases();

  EXPECT_EQ(container, mainFrame()->document()->rootScroller());
  EXPECT_NE(container, effectiveRootScroller(mainFrame()->document()));
}

// Tests that setting an element that isn't a valid scroller as the root
// scroller doesn't change the effective root scroller.
TEST_F(RootScrollerTest, TestSetRootScrollerOnInvalidElement) {
  initialize("root-scroller.html");

  {
    // Set to a non-block element. Should be rejected and a console message
    // logged.
    Element* element = mainFrame()->document()->getElementById("nonBlock");
    TrackExceptionState exceptionState;
    mainFrame()->document()->setRootScroller(element, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();
    EXPECT_EQ(element, mainFrame()->document()->rootScroller());
    EXPECT_NE(element, effectiveRootScroller(mainFrame()->document()));
  }

  {
    // Set to an element with no size.
    Element* element = mainFrame()->document()->getElementById("empty");
    TrackExceptionState exceptionState;
    mainFrame()->document()->setRootScroller(element, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();
    EXPECT_EQ(element, mainFrame()->document()->rootScroller());
    EXPECT_NE(element, effectiveRootScroller(mainFrame()->document()));
  }
}

// Test that the effective root scroller resets to the default element when the
// current root scroller element becomes invalid as a scroller.
TEST_F(RootScrollerTest, TestRootScrollerBecomesInvalid) {
  initialize("root-scroller.html");

  Element* htmlElement = mainFrame()->document()->documentElement();
  Element* container = mainFrame()->document()->getElementById("container");
  TrackExceptionState exceptionState;

  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());
  ASSERT_EQ(htmlElement, effectiveRootScroller(mainFrame()->document()));

  {
    mainFrame()->document()->setRootScroller(container, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(container, mainFrame()->document()->rootScroller());
    EXPECT_EQ(container, effectiveRootScroller(mainFrame()->document()));

    executeScript(
        "document.querySelector('#container').style.display = 'inline'");
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(container, mainFrame()->document()->rootScroller());
    EXPECT_EQ(htmlElement, effectiveRootScroller(mainFrame()->document()));
  }

  executeScript("document.querySelector('#container').style.display = 'block'");
  mainFrame()->document()->setRootScroller(nullptr, exceptionState);
  mainFrameView()->updateAllLifecyclePhases();
  EXPECT_EQ(nullptr, mainFrame()->document()->rootScroller());
  EXPECT_EQ(htmlElement, effectiveRootScroller(mainFrame()->document()));

  {
    mainFrame()->document()->setRootScroller(container, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(container, mainFrame()->document()->rootScroller());
    EXPECT_EQ(container, effectiveRootScroller(mainFrame()->document()));

    executeScript("document.querySelector('#container').style.width = '98%'");
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(container, mainFrame()->document()->rootScroller());
    EXPECT_EQ(htmlElement, effectiveRootScroller(mainFrame()->document()));
  }
}

// Tests that setting the root scroller of the top document to an element that
// belongs to a nested document works.
TEST_F(RootScrollerTest, TestSetRootScrollerOnElementInIframe) {
  initialize("root-scroller-iframe.html");

  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());

  {
    // Trying to set an element from a nested document should fail.
    HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
        mainFrame()->document()->getElementById("iframe"));
    Element* innerContainer =
        iframe->contentDocument()->getElementById("container");

    TrackExceptionState exceptionState;
    mainFrame()->document()->setRootScroller(innerContainer, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(innerContainer, mainFrame()->document()->rootScroller());
    EXPECT_EQ(innerContainer, effectiveRootScroller(mainFrame()->document()));
  }

  {
    // Setting the iframe itself should also work.
    HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
        mainFrame()->document()->getElementById("iframe"));

    TrackExceptionState exceptionState;
    mainFrame()->document()->setRootScroller(iframe, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(iframe, mainFrame()->document()->rootScroller());
    EXPECT_EQ(iframe, effectiveRootScroller(mainFrame()->document()));
  }
}

// Tests that setting a valid element as the root scroller on a document within
// an iframe works as expected.
TEST_F(RootScrollerTest, TestRootScrollerWithinIframe) {
  initialize("root-scroller-iframe.html");

  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());

  {
    HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
        mainFrame()->document()->getElementById("iframe"));

    EXPECT_EQ(iframe->contentDocument()->documentElement(),
              effectiveRootScroller(iframe->contentDocument()));

    Element* innerContainer =
        iframe->contentDocument()->getElementById("container");
    TrackExceptionState exceptionState;
    iframe->contentDocument()->setRootScroller(innerContainer, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(innerContainer, iframe->contentDocument()->rootScroller());
    EXPECT_EQ(innerContainer, effectiveRootScroller(iframe->contentDocument()));
  }
}

// Tests that setting an iframe as the root scroller makes the iframe the
// effective root scroller in the parent frame.
TEST_F(RootScrollerTest, SetRootScrollerIframeBecomesEffective) {
  initialize("root-scroller-iframe.html");
  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());

  {
    NonThrowableExceptionState nonThrow;

    // Try to set the root scroller in the main frame to be the iframe
    // element.
    HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
        mainFrame()->document()->getElementById("iframe"));

    mainFrame()->document()->setRootScroller(iframe, nonThrow);

    EXPECT_EQ(iframe, mainFrame()->document()->rootScroller());
    EXPECT_EQ(iframe, mainFrame()
                          ->document()
                          ->rootScrollerController()
                          ->effectiveRootScroller());

    Element* container = iframe->contentDocument()->getElementById("container");

    iframe->contentDocument()->setRootScroller(container, nonThrow);

    EXPECT_EQ(container, iframe->contentDocument()->rootScroller());
    EXPECT_EQ(container, iframe->contentDocument()
                             ->rootScrollerController()
                             ->effectiveRootScroller());
    EXPECT_EQ(iframe, mainFrame()->document()->rootScroller());
    EXPECT_EQ(iframe, mainFrame()
                          ->document()
                          ->rootScrollerController()
                          ->effectiveRootScroller());
  }
}

// Tests that the global root scroller is correctly calculated when getting the
// root scroller layer and that the viewport apply scroll is set on it.
TEST_F(RootScrollerTest, SetRootScrollerIframeUsesCorrectLayerAndCallback) {
  // TODO(bokan): The expectation and actual in the checks here are backwards.
  initialize("root-scroller-iframe.html");
  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());

  HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
      mainFrame()->document()->getElementById("iframe"));
  Element* container = iframe->contentDocument()->getElementById("container");

  const TopDocumentRootScrollerController& mainController =
      mainFrame()->document()->frameHost()->globalRootScrollerController();

  NonThrowableExceptionState nonThrow;

  // No root scroller set, the documentElement should be the effective root
  // and the main FrameView's scroll layer should be the layer to use.
  {
    EXPECT_EQ(mainController.rootScrollerLayer(),
              mainFrameView()->layerForScrolling());
    EXPECT_TRUE(mainController.isViewportScrollCallback(
        mainFrame()->document()->documentElement()->getApplyScroll()));
  }

  // Set a root scroller in the iframe. Since the main document didn't set a
  // root scroller, the global root scroller shouldn't change.
  {
    iframe->contentDocument()->setRootScroller(container, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    EXPECT_EQ(mainController.rootScrollerLayer(),
              mainFrameView()->layerForScrolling());
    EXPECT_TRUE(mainController.isViewportScrollCallback(
        mainFrame()->document()->documentElement()->getApplyScroll()));
  }

  // Setting the iframe as the root scroller in the main frame should now
  // link the root scrollers so the container should now be the global root
  // scroller.
  {
    mainFrame()->document()->setRootScroller(iframe, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    ScrollableArea* containerScroller =
        static_cast<PaintInvalidationCapableScrollableArea*>(
            toLayoutBox(container->layoutObject())->getScrollableArea());

    EXPECT_EQ(mainController.rootScrollerLayer(),
              containerScroller->layerForScrolling());
    EXPECT_FALSE(mainController.isViewportScrollCallback(
        mainFrame()->document()->documentElement()->getApplyScroll()));
    EXPECT_TRUE(
        mainController.isViewportScrollCallback(container->getApplyScroll()));
  }

  // Unsetting the root scroller in the iframe should reset its effective
  // root scroller to the iframe's documentElement and thus the iframe's
  // documentElement becomes the global root scroller.
  {
    iframe->contentDocument()->setRootScroller(nullptr, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();
    EXPECT_EQ(mainController.rootScrollerLayer(),
              iframe->contentDocument()->view()->layerForScrolling());
    EXPECT_FALSE(
        mainController.isViewportScrollCallback(container->getApplyScroll()));
    EXPECT_FALSE(mainController.isViewportScrollCallback(
        mainFrame()->document()->documentElement()->getApplyScroll()));
    EXPECT_TRUE(mainController.isViewportScrollCallback(
        iframe->contentDocument()->documentElement()->getApplyScroll()));
  }

  // Finally, unsetting the main frame's root scroller should reset it to the
  // documentElement and corresponding layer.
  {
    mainFrame()->document()->setRootScroller(nullptr, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();
    EXPECT_EQ(mainController.rootScrollerLayer(),
              mainFrameView()->layerForScrolling());
    EXPECT_TRUE(mainController.isViewportScrollCallback(
        mainFrame()->document()->documentElement()->getApplyScroll()));
    EXPECT_FALSE(
        mainController.isViewportScrollCallback(container->getApplyScroll()));
    EXPECT_FALSE(mainController.isViewportScrollCallback(
        iframe->contentDocument()->documentElement()->getApplyScroll()));
  }
}

TEST_F(RootScrollerTest, TestSetRootScrollerCausesViewportLayerChange) {
  // TODO(bokan): Need a test that changing root scrollers actually sets the
  // outer viewport layer on the compositor, even in the absence of other
  // compositing changes. crbug.com/505516
}

// Tests that trying to set an element as the root scroller of a document inside
// an iframe fails when that element belongs to the parent document.
// TODO(bokan): Recent changes mean this is now possible but should be fixed.
TEST_F(RootScrollerTest,
       DISABLED_TestSetRootScrollerOnElementFromOutsideIframe) {
  initialize("root-scroller-iframe.html");

  ASSERT_EQ(nullptr, mainFrame()->document()->rootScroller());
  {
    // Try to set the the root scroller of the child document to be the
    // <iframe> element in the parent document.
    HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
        mainFrame()->document()->getElementById("iframe"));
    NonThrowableExceptionState nonThrow;
    Element* body = mainFrame()->document()->querySelector("body", nonThrow);

    EXPECT_EQ(nullptr, iframe->contentDocument()->rootScroller());

    TrackExceptionState exceptionState;
    iframe->contentDocument()->setRootScroller(iframe, exceptionState);

    EXPECT_EQ(iframe, iframe->contentDocument()->rootScroller());

    // Try to set the root scroller of the child document to be the
    // <body> element of the parent document.
    iframe->contentDocument()->setRootScroller(body, exceptionState);

    EXPECT_EQ(body, iframe->contentDocument()->rootScroller());
  }
}

// Do a basic sanity check that setting as root scroller an iframe that's remote
// doesn't crash or otherwise fail catastrophically.
TEST_F(RootScrollerTest, RemoteIFrame) {
  FrameTestHelpers::TestWebRemoteFrameClient remoteFrameClient;
  initialize("root-scroller-iframe.html");

  // Initialization: Replace the iframe with a remote frame.
  {
    WebRemoteFrame* remoteFrame =
        WebRemoteFrame::create(WebTreeScopeType::Document, &remoteFrameClient);
    WebFrame* childFrame = mainWebFrame()->firstChild();
    childFrame->swap(remoteFrame);
  }

  // Set the root scroller in the local main frame to the iframe (which is
  // remote).
  {
    Element* iframe = mainFrame()->document()->getElementById("iframe");
    NonThrowableExceptionState nonThrow;
    mainFrame()->document()->setRootScroller(iframe, nonThrow);
    EXPECT_EQ(iframe, mainFrame()->document()->rootScroller());
  }

  // Reset explicitly to prevent lifetime issues with the RemoteFrameClient.
  m_helper.reset();
}

// Do a basic sanity check that the scrolling and root scroller machinery
// doesn't fail catastrophically in site isolation when the main frame is
// remote. Setting a root scroller in OOPIF isn't implemented yet but we should
// still scroll as before and not crash.
TEST_F(RootScrollerTest, RemoteMainFrame) {
  FrameTestHelpers::TestWebRemoteFrameClient remoteClient;
  FrameTestHelpers::TestWebWidgetClient webWidgetClient;
  WebFrameWidget* widget;
  WebLocalFrameImpl* localFrame;

  initialize("root-scroller-iframe.html");

  // Initialization: Set the main frame to be a RemoteFrame and add a local
  // child.
  {
    webViewImpl()->setMainFrame(remoteClient.frame());
    WebRemoteFrame* root = webViewImpl()->mainFrame()->toWebRemoteFrame();
    root->setReplicatedOrigin(SecurityOrigin::createUnique());
    WebFrameOwnerProperties properties;
    localFrame = FrameTestHelpers::createLocalChild(
        root, "frameName", nullptr, nullptr, nullptr, properties);

    FrameTestHelpers::loadFrame(localFrame,
                                m_baseURL + "root-scroller-child.html");
    widget = localFrame->frameWidget();
    widget->resize(WebSize(400, 400));
  }

  Document* document = localFrame->frameView()->frame().document();
  Element* container = document->getElementById("container");

  // Try scrolling in the iframe.
  {
    widget->handleInputEvent(
        generateWheelGestureEvent(WebInputEvent::GestureScrollBegin));
    widget->handleInputEvent(
        generateWheelGestureEvent(WebInputEvent::GestureScrollUpdate, 0, -100));
    widget->handleInputEvent(
        generateWheelGestureEvent(WebInputEvent::GestureScrollEnd));
    EXPECT_EQ(100, container->scrollTop());
  }

  // Set the container Element as the root scroller.
  {
    NonThrowableExceptionState nonThrow;
    document->setRootScroller(container, nonThrow);
    EXPECT_EQ(container, document->rootScroller());
  }

  // Try scrolling in the iframe now that it has a root scroller set.
  {
    widget->handleInputEvent(
        generateWheelGestureEvent(WebInputEvent::GestureScrollBegin));
    widget->handleInputEvent(
        generateWheelGestureEvent(WebInputEvent::GestureScrollUpdate, 0, -100));
    widget->handleInputEvent(
        generateWheelGestureEvent(WebInputEvent::GestureScrollEnd));

    // TODO(bokan): This doesn't work right now because we notice in
    // Element::nativeApplyScroll that the container is the
    // effectiveRootScroller but the only way we expect to get to
    // nativeApplyScroll is if the effective scroller had its applyScroll
    // ViewportScrollCallback removed.  Keep the scrolls to guard crashes
    // but the expectations on when a ViewportScrollCallback have changed
    // and should be updated.
    // EXPECT_EQ(200, container->scrollTop());
  }

  // Reset explicitly to prevent lifetime issues with the RemoteFrameClient.
  m_helper.reset();
}

// Tests that clipping layers belonging to any compositors in the ancestor chain
// of the global root scroller have their masking bit removed.
TEST_F(RootScrollerTest, RemoveClippingOnCompositorLayers) {
  initialize("root-scroller-iframe.html");

  HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
      mainFrame()->document()->getElementById("iframe"));
  Element* container = iframe->contentDocument()->getElementById("container");

  RootScrollerController* mainController =
      mainFrame()->document()->rootScrollerController();
  RootScrollerController* childController =
      iframe->contentDocument()->rootScrollerController();
  TopDocumentRootScrollerController& globalController =
      frameHost().globalRootScrollerController();

  PaintLayerCompositor* mainCompositor =
      mainFrameView()->layoutViewItem().compositor();
  PaintLayerCompositor* childCompositor =
      iframe->contentDocument()->view()->layoutViewItem().compositor();

  NonThrowableExceptionState nonThrow;

  // No root scroller set, on the main frame the root content layer should
  // clip. Additionally, on the child frame, the overflow controls host and
  // container layers should also clip.
  {
    EXPECT_TRUE(
        mainCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->containerLayer()->platformLayer()->masksToBounds());

    EXPECT_TRUE(
        childCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_TRUE(
        childCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_TRUE(
        childCompositor->containerLayer()->platformLayer()->masksToBounds());
  }

  // Now set the root scrollers such that the container in the iframe is the
  // global root scroller. All the previously clipping layers in both paint
  // layer compositors should no longer clip.
  {
    iframe->contentDocument()->setRootScroller(container, nonThrow);
    mainFrame()->document()->setRootScroller(iframe, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    ASSERT_EQ(iframe, mainController->effectiveRootScroller());
    ASSERT_EQ(container, childController->effectiveRootScroller());

    EXPECT_FALSE(
        mainCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->containerLayer()->platformLayer()->masksToBounds());

    EXPECT_FALSE(
        childCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->containerLayer()->platformLayer()->masksToBounds());
  }

  // Now reset the iframe's root scroller. Since the iframe itself is now the
  // global root scroller we want it to behave as if it were the main frame,
  // which means it should clip only on its root content layer.
  {
    iframe->contentDocument()->setRootScroller(nullptr, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    ASSERT_EQ(iframe, mainController->effectiveRootScroller());
    ASSERT_EQ(iframe->contentDocument()->documentElement(),
              childController->effectiveRootScroller());
    ASSERT_EQ(iframe->contentDocument()->documentElement(),
              globalController.globalRootScroller());

    EXPECT_FALSE(
        mainCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->containerLayer()->platformLayer()->masksToBounds());

    EXPECT_TRUE(
        childCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->containerLayer()->platformLayer()->masksToBounds());
  }

  // Now reset the main frame's root scroller. Its compositor should go back
  // to clipping as well. Because the iframe is now no longer the global root
  // scroller, it should go back to clipping its overflow host and container
  // layers. This checks that we invalidate the compositing state even though
  // the iframe's effective root scroller hasn't changed.

  {
    mainFrame()->document()->setRootScroller(nullptr, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    ASSERT_EQ(mainFrame()->document()->documentElement(),
              mainController->effectiveRootScroller());
    ASSERT_EQ(iframe->contentDocument()->documentElement(),
              childController->effectiveRootScroller());
    ASSERT_EQ(mainFrame()->document()->documentElement(),
              globalController.globalRootScroller());

    EXPECT_TRUE(
        mainCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->containerLayer()->platformLayer()->masksToBounds());

    EXPECT_TRUE(
        childCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_TRUE(
        childCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_TRUE(
        childCompositor->containerLayer()->platformLayer()->masksToBounds());
  }

  // Set the iframe back as the main frame's root scroller. Since its the
  // global root scroller again, it should clip like the root frame. This
  // checks that we invalidate the compositing state even though the iframe's
  // effective root scroller hasn't changed.
  {
    mainFrame()->document()->setRootScroller(iframe, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    ASSERT_EQ(iframe, mainController->effectiveRootScroller());
    ASSERT_EQ(iframe->contentDocument()->documentElement(),
              childController->effectiveRootScroller());
    ASSERT_EQ(iframe->contentDocument()->documentElement(),
              globalController.globalRootScroller());

    EXPECT_FALSE(
        mainCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->containerLayer()->platformLayer()->masksToBounds());

    EXPECT_TRUE(
        childCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->containerLayer()->platformLayer()->masksToBounds());
  }

  // Set just the iframe's root scroller. We should stop clipping the
  // iframe's compositor's layers but not the main frame's.
  {
    mainFrame()->document()->setRootScroller(nullptr, nonThrow);
    iframe->contentDocument()->setRootScroller(container, nonThrow);
    mainFrameView()->updateAllLifecyclePhases();

    ASSERT_EQ(mainFrame()->document()->documentElement(),
              mainController->effectiveRootScroller());
    ASSERT_EQ(container, childController->effectiveRootScroller());

    EXPECT_TRUE(
        mainCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        mainCompositor->containerLayer()->platformLayer()->masksToBounds());

    EXPECT_FALSE(
        childCompositor->rootContentLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->rootGraphicsLayer()->platformLayer()->masksToBounds());
    EXPECT_FALSE(
        childCompositor->containerLayer()->platformLayer()->masksToBounds());
  }
}

// Tests that removing the root scroller element from the DOM resets the
// effective root scroller without waiting for any lifecycle events.
TEST_F(RootScrollerTest, RemoveRootScrollerFromDom) {
  initialize("root-scroller-iframe.html");

  {
    HTMLFrameOwnerElement* iframe = toHTMLFrameOwnerElement(
        mainFrame()->document()->getElementById("iframe"));
    Element* innerContainer =
        iframe->contentDocument()->getElementById("container");

    NonThrowableExceptionState exceptionState;
    mainFrame()->document()->setRootScroller(iframe, exceptionState);
    iframe->contentDocument()->setRootScroller(innerContainer, exceptionState);
    mainFrameView()->updateAllLifecyclePhases();

    ASSERT_EQ(iframe, mainFrame()->document()->rootScroller());
    ASSERT_EQ(iframe, effectiveRootScroller(mainFrame()->document()));
    ASSERT_EQ(innerContainer, iframe->contentDocument()->rootScroller());
    ASSERT_EQ(innerContainer, effectiveRootScroller(iframe->contentDocument()));

    iframe->contentDocument()->body()->setInnerHTML("", exceptionState);

    // If the root scroller wasn't updated by the DOM removal above, this
    // will touch the disposed root scroller's ScrollableArea.
    mainFrameView()->getRootFrameViewport()->serviceScrollAnimations(0);

    EXPECT_EQ(iframe->contentDocument()->documentElement(),
              effectiveRootScroller(iframe->contentDocument()));
  }
}

// Tests that we still have a global root scroller layer when the HTML element
// has no layout object. crbug.com/637036.
TEST_F(RootScrollerTest, DocumentElementHasNoLayoutObject) {
  initialize("overflow-scrolling.html");

  // There's no rootScroller set on this page so we should default to the <html>
  // element, which means we should use the layout viewport. Ensure this happens
  // even if the <html> element has no LayoutObject.
  executeScript("document.documentElement.style.display = 'none';");

  const TopDocumentRootScrollerController& globalController =
      mainFrame()->document()->frameHost()->globalRootScrollerController();

  EXPECT_EQ(mainFrame()->document()->documentElement(),
            globalController.globalRootScroller());
  EXPECT_EQ(
      mainFrameView()->layoutViewportScrollableArea()->layerForScrolling(),
      globalController.rootScrollerLayer());
}

}  // namespace

}  // namespace blink
