// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/WebRemoteFrameImpl.h"

#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/layout/LayoutObject.h"
#include "core/page/Page.h"
#include "platform/heap/Handle.h"
#include "public/platform/WebFloatRect.h"
#include "public/platform/WebRect.h"
#include "public/web/WebDocument.h"
#include "public/web/WebFrameOwnerProperties.h"
#include "public/web/WebPerformance.h"
#include "public/web/WebRange.h"
#include "public/web/WebTreeScopeType.h"
#include "web/RemoteFrameOwner.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebViewImpl.h"
#include <v8/include/v8.h>

namespace blink {

WebRemoteFrame* WebRemoteFrame::create(WebTreeScopeType scope, WebRemoteFrameClient* client, WebFrame* opener)
{
    return WebRemoteFrameImpl::create(scope, client, opener);
}

WebRemoteFrameImpl* WebRemoteFrameImpl::create(WebTreeScopeType scope, WebRemoteFrameClient* client, WebFrame* opener)
{
    WebRemoteFrameImpl* frame = new WebRemoteFrameImpl(scope, client);
    frame->setOpener(opener);
#if ENABLE(OILPAN)
    return frame;
#else
    return adoptRef(frame).leakRef();
#endif
}

WebRemoteFrameImpl::~WebRemoteFrameImpl()
{
}

#if ENABLE(OILPAN)
DEFINE_TRACE(WebRemoteFrameImpl)
{
    visitor->trace(m_frameClient);
    visitor->trace(m_frame);
    visitor->template registerWeakMembers<WebFrame, &WebFrame::clearWeakFrames>(this);
    WebFrame::traceFrames(visitor, this);
    WebFrameImplBase::trace(visitor);
}
#endif

bool WebRemoteFrameImpl::isWebLocalFrame() const
{
    return false;
}

WebLocalFrame* WebRemoteFrameImpl::toWebLocalFrame()
{
    ASSERT_NOT_REACHED();
    return nullptr;
}

bool WebRemoteFrameImpl::isWebRemoteFrame() const
{
    return true;
}

WebRemoteFrame* WebRemoteFrameImpl::toWebRemoteFrame()
{
    return this;
}

void WebRemoteFrameImpl::close()
{
#if ENABLE(OILPAN)
    m_selfKeepAlive.clear();
#else
    deref();
#endif
}

WebString WebRemoteFrameImpl::uniqueName() const
{
    ASSERT_NOT_REACHED();
    return WebString();
}

WebString WebRemoteFrameImpl::assignedName() const
{
    ASSERT_NOT_REACHED();
    return WebString();
}

void WebRemoteFrameImpl::setName(const WebString&)
{
    ASSERT_NOT_REACHED();
}

WebVector<WebIconURL> WebRemoteFrameImpl::iconURLs(int iconTypesMask) const
{
    ASSERT_NOT_REACHED();
    return WebVector<WebIconURL>();
}

void WebRemoteFrameImpl::setRemoteWebLayer(WebLayer* webLayer)
{
    if (!frame())
        return;

    frame()->setRemotePlatformLayer(webLayer);
}

void WebRemoteFrameImpl::setSharedWorkerRepositoryClient(WebSharedWorkerRepositoryClient*)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::setCanHaveScrollbars(bool)
{
    ASSERT_NOT_REACHED();
}

WebSize WebRemoteFrameImpl::scrollOffset() const
{
    ASSERT_NOT_REACHED();
    return WebSize();
}

void WebRemoteFrameImpl::setScrollOffset(const WebSize&)
{
    ASSERT_NOT_REACHED();
}

WebSize WebRemoteFrameImpl::contentsSize() const
{
    ASSERT_NOT_REACHED();
    return WebSize();
}

bool WebRemoteFrameImpl::hasVisibleContent() const
{
    ASSERT_NOT_REACHED();
    return false;
}

WebRect WebRemoteFrameImpl::visibleContentRect() const
{
    ASSERT_NOT_REACHED();
    return WebRect();
}

bool WebRemoteFrameImpl::hasHorizontalScrollbar() const
{
    ASSERT_NOT_REACHED();
    return false;
}

bool WebRemoteFrameImpl::hasVerticalScrollbar() const
{
    ASSERT_NOT_REACHED();
    return false;
}

WebView* WebRemoteFrameImpl::view() const
{
    if (!frame())
        return nullptr;
    return WebViewImpl::fromPage(frame()->page());
}

WebDocument WebRemoteFrameImpl::document() const
{
    // TODO(dcheng): this should also ASSERT_NOT_REACHED, but a lot of
    // code tries to access the document of a remote frame at the moment.
    return WebDocument();
}

WebPerformance WebRemoteFrameImpl::performance() const
{
    ASSERT_NOT_REACHED();
    return WebPerformance();
}

bool WebRemoteFrameImpl::dispatchBeforeUnloadEvent()
{
    ASSERT_NOT_REACHED();
    return false;
}

void WebRemoteFrameImpl::dispatchUnloadEvent()
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::executeScript(const WebScriptSource&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::executeScriptInIsolatedWorld(
    int worldID, const WebScriptSource* sources, unsigned numSources,
    int extensionGroup)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::setIsolatedWorldSecurityOrigin(int worldID, const WebSecurityOrigin&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::setIsolatedWorldContentSecurityPolicy(int worldID, const WebString&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::addMessageToConsole(const WebConsoleMessage&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::collectGarbage()
{
    ASSERT_NOT_REACHED();
}

v8::Local<v8::Value> WebRemoteFrameImpl::executeScriptAndReturnValue(
    const WebScriptSource&)
{
    ASSERT_NOT_REACHED();
    return v8::Local<v8::Value>();
}

void WebRemoteFrameImpl::executeScriptInIsolatedWorld(
    int worldID, const WebScriptSource* sourcesIn, unsigned numSources,
    int extensionGroup, WebVector<v8::Local<v8::Value>>* results)
{
    ASSERT_NOT_REACHED();
}

v8::Local<v8::Value> WebRemoteFrameImpl::callFunctionEvenIfScriptDisabled(
    v8::Local<v8::Function>,
    v8::Local<v8::Value>,
    int argc,
    v8::Local<v8::Value> argv[])
{
    ASSERT_NOT_REACHED();
    return v8::Local<v8::Value>();
}

v8::Local<v8::Context> WebRemoteFrameImpl::mainWorldScriptContext() const
{
    ASSERT_NOT_REACHED();
    return v8::Local<v8::Context>();
}

v8::Local<v8::Context> WebRemoteFrameImpl::deprecatedMainWorldScriptContext() const
{
    return toV8Context(frame(), DOMWrapperWorld::mainWorld());
}

void WebRemoteFrameImpl::reload(WebFrameLoadType)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::reloadWithOverrideURL(const WebURL& overrideUrl, WebFrameLoadType)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::loadRequest(const WebURLRequest&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::loadHistoryItem(const WebHistoryItem&, WebHistoryLoadType, WebCachePolicy)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::loadHTMLString(
    const WebData& html, const WebURL& baseURL, const WebURL& unreachableURL,
    bool replace)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::stopLoading()
{
    // TODO(dcheng,japhet): Calling this method should stop loads
    // in all subframes, both remote and local.
}

WebDataSource* WebRemoteFrameImpl::provisionalDataSource() const
{
    ASSERT_NOT_REACHED();
    return nullptr;
}

WebDataSource* WebRemoteFrameImpl::dataSource() const
{
    ASSERT_NOT_REACHED();
    return nullptr;
}

void WebRemoteFrameImpl::enableViewSourceMode(bool enable)
{
    ASSERT_NOT_REACHED();
}

bool WebRemoteFrameImpl::isViewSourceModeEnabled() const
{
    ASSERT_NOT_REACHED();
    return false;
}

void WebRemoteFrameImpl::setReferrerForRequest(WebURLRequest&, const WebURL& referrer)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::dispatchWillSendRequest(WebURLRequest&)
{
    ASSERT_NOT_REACHED();
}

WebURLLoader* WebRemoteFrameImpl::createAssociatedURLLoader(const WebURLLoaderOptions&)
{
    ASSERT_NOT_REACHED();
    return nullptr;
}

unsigned WebRemoteFrameImpl::unloadListenerCount() const
{
    ASSERT_NOT_REACHED();
    return 0;
}

void WebRemoteFrameImpl::insertText(const WebString&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::setMarkedText(const WebString&, unsigned location, unsigned length)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::unmarkText()
{
    ASSERT_NOT_REACHED();
}

bool WebRemoteFrameImpl::hasMarkedText() const
{
    ASSERT_NOT_REACHED();
    return false;
}

WebRange WebRemoteFrameImpl::markedRange() const
{
    ASSERT_NOT_REACHED();
    return WebRange();
}

bool WebRemoteFrameImpl::firstRectForCharacterRange(unsigned location, unsigned length, WebRect&) const
{
    ASSERT_NOT_REACHED();
    return false;
}

size_t WebRemoteFrameImpl::characterIndexForPoint(const WebPoint&) const
{
    ASSERT_NOT_REACHED();
    return 0;
}

bool WebRemoteFrameImpl::executeCommand(const WebString&, const WebNode&)
{
    ASSERT_NOT_REACHED();
    return false;
}

bool WebRemoteFrameImpl::executeCommand(const WebString&, const WebString& value, const WebNode&)
{
    ASSERT_NOT_REACHED();
    return false;
}

bool WebRemoteFrameImpl::isCommandEnabled(const WebString&) const
{
    ASSERT_NOT_REACHED();
    return false;
}

void WebRemoteFrameImpl::enableContinuousSpellChecking(bool)
{
}

bool WebRemoteFrameImpl::isContinuousSpellCheckingEnabled() const
{
    return false;
}

void WebRemoteFrameImpl::requestTextChecking(const WebElement&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::removeSpellingMarkers()
{
    ASSERT_NOT_REACHED();
}

bool WebRemoteFrameImpl::hasSelection() const
{
    ASSERT_NOT_REACHED();
    return false;
}

WebRange WebRemoteFrameImpl::selectionRange() const
{
    ASSERT_NOT_REACHED();
    return WebRange();
}

WebString WebRemoteFrameImpl::selectionAsText() const
{
    ASSERT_NOT_REACHED();
    return WebString();
}

WebString WebRemoteFrameImpl::selectionAsMarkup() const
{
    ASSERT_NOT_REACHED();
    return WebString();
}

bool WebRemoteFrameImpl::selectWordAroundCaret()
{
    ASSERT_NOT_REACHED();
    return false;
}

void WebRemoteFrameImpl::selectRange(const WebPoint& base, const WebPoint& extent)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::selectRange(const WebRange&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::moveRangeSelection(const WebPoint& base, const WebPoint& extent, WebFrame::TextGranularity granularity)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::moveCaretSelection(const WebPoint&)
{
    ASSERT_NOT_REACHED();
}

bool WebRemoteFrameImpl::setEditableSelectionOffsets(int start, int end)
{
    ASSERT_NOT_REACHED();
    return false;
}

bool WebRemoteFrameImpl::setCompositionFromExistingText(int compositionStart, int compositionEnd, const WebVector<WebCompositionUnderline>& underlines)
{
    ASSERT_NOT_REACHED();
    return false;
}

void WebRemoteFrameImpl::extendSelectionAndDelete(int before, int after)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::setCaretVisible(bool)
{
    ASSERT_NOT_REACHED();
}

int WebRemoteFrameImpl::printBegin(const WebPrintParams&, const WebNode& constrainToNode)
{
    ASSERT_NOT_REACHED();
    return 0;
}

float WebRemoteFrameImpl::printPage(int pageToPrint, WebCanvas*)
{
    ASSERT_NOT_REACHED();
    return 0.0;
}

float WebRemoteFrameImpl::getPrintPageShrink(int page)
{
    ASSERT_NOT_REACHED();
    return 0.0;
}

void WebRemoteFrameImpl::printEnd()
{
    ASSERT_NOT_REACHED();
}

bool WebRemoteFrameImpl::isPrintScalingDisabledForPlugin(const WebNode&)
{
    ASSERT_NOT_REACHED();
    return false;
}

bool WebRemoteFrameImpl::hasCustomPageSizeStyle(int pageIndex)
{
    ASSERT_NOT_REACHED();
    return false;
}

bool WebRemoteFrameImpl::isPageBoxVisible(int pageIndex)
{
    ASSERT_NOT_REACHED();
    return false;
}

void WebRemoteFrameImpl::pageSizeAndMarginsInPixels(
    int pageIndex,
    WebSize& pageSize,
    int& marginTop,
    int& marginRight,
    int& marginBottom,
    int& marginLeft)
{
    ASSERT_NOT_REACHED();
}

WebString WebRemoteFrameImpl::pageProperty(const WebString& propertyName, int pageIndex)
{
    ASSERT_NOT_REACHED();
    return WebString();
}

void WebRemoteFrameImpl::printPagesWithBoundaries(WebCanvas*, const WebSize&)
{
    ASSERT_NOT_REACHED();
}

void WebRemoteFrameImpl::dispatchMessageEventWithOriginCheck(
    const WebSecurityOrigin& intendedTargetOrigin,
    const WebDOMEvent&)
{
    ASSERT_NOT_REACHED();
}

WebRect WebRemoteFrameImpl::selectionBoundsRect() const
{
    ASSERT_NOT_REACHED();
    return WebRect();
}

bool WebRemoteFrameImpl::selectionStartHasSpellingMarkerFor(int from, int length) const
{
    ASSERT_NOT_REACHED();
    return false;
}

WebString WebRemoteFrameImpl::layerTreeAsText(bool showDebugInfo) const
{
    ASSERT_NOT_REACHED();
    return WebString();
}

WebLocalFrame* WebRemoteFrameImpl::createLocalChild(WebTreeScopeType scope, const WebString& name, const WebString& uniqueName, WebSandboxFlags sandboxFlags, WebFrameClient* client, WebFrame* previousSibling, const WebFrameOwnerProperties& frameOwnerProperties, WebFrame* opener)
{
    WebLocalFrameImpl* child = WebLocalFrameImpl::create(scope, client, opener);
    insertAfter(child, previousSibling);
    RemoteFrameOwner* owner = RemoteFrameOwner::create(static_cast<SandboxFlags>(sandboxFlags), frameOwnerProperties);
    // FIXME: currently this calls LocalFrame::init() on the created LocalFrame, which may
    // result in the browser observing two navigations to about:blank (one from the initial
    // frame creation, and one from swapping it into the remote process). FrameLoader might
    // need a special initialization function for this case to avoid that duplicate navigation.
    child->initializeCoreFrame(frame()->host(), owner, name, uniqueName);
    // Partially related with the above FIXME--the init() call may trigger JS dispatch. However,
    // if the parent is remote, it should never be detached synchronously...
    DCHECK(child->frame());
    return child;
}

void WebRemoteFrameImpl::initializeCoreFrame(FrameHost* host, FrameOwner* owner, const AtomicString& name, const AtomicString& uniqueName)
{
    setCoreFrame(RemoteFrame::create(m_frameClient.get(), host, owner));
    frame()->createView();
    m_frame->tree().setPrecalculatedName(name, uniqueName);
}

WebRemoteFrame* WebRemoteFrameImpl::createRemoteChild(WebTreeScopeType scope, const WebString& name, const WebString& uniqueName, WebSandboxFlags sandboxFlags, WebRemoteFrameClient* client, WebFrame* opener)
{
    WebRemoteFrameImpl* child = WebRemoteFrameImpl::create(scope, client, opener);
    appendChild(child);
    RemoteFrameOwner* owner = RemoteFrameOwner::create(static_cast<SandboxFlags>(sandboxFlags), WebFrameOwnerProperties());
    child->initializeCoreFrame(frame()->host(), owner, name, uniqueName);
    return child;
}

void WebRemoteFrameImpl::setCoreFrame(RemoteFrame* frame)
{
    m_frame = frame;
}

WebRemoteFrameImpl* WebRemoteFrameImpl::fromFrame(RemoteFrame& frame)
{
    if (!frame.client())
        return nullptr;
    return static_cast<RemoteFrameClientImpl*>(frame.client())->webFrame();
}

void WebRemoteFrameImpl::initializeFromFrame(WebLocalFrame* source) const
{
    DCHECK(source);
    WebLocalFrameImpl* localFrameImpl = toWebLocalFrameImpl(source);

    client()->initializeChildFrame(
        localFrameImpl->frame()->view()->frameRect(),
        localFrameImpl->frame()->page()->deviceScaleFactor());
}

void WebRemoteFrameImpl::setReplicatedOrigin(const WebSecurityOrigin& origin) const
{
    DCHECK(frame());
    frame()->securityContext()->setReplicatedOrigin(origin);

    // If the origin of a remote frame changed, the accessibility object for the owner
    // element now points to a different child.
    //
    // TODO(dmazzoni, dcheng): there's probably a better way to solve this.
    // Run SitePerProcessAccessibilityBrowserTest.TwoCrossSiteNavigations to
    // ensure an alternate fix works.  http://crbug.com/566222
    FrameOwner* owner = frame()->owner();
    if (owner && owner->isLocal()) {
        HTMLElement* ownerElement = toHTMLFrameOwnerElement(owner);
        AXObjectCache* cache = ownerElement->document().existingAXObjectCache();
        if (cache)
            cache->childrenChanged(ownerElement);
    }
}

void WebRemoteFrameImpl::setReplicatedSandboxFlags(WebSandboxFlags flags) const
{
    DCHECK(frame());
    frame()->securityContext()->enforceSandboxFlags(static_cast<SandboxFlags>(flags));
}

void WebRemoteFrameImpl::setReplicatedName(const WebString& name, const WebString& uniqueName) const
{
    DCHECK(frame());
    frame()->tree().setPrecalculatedName(name, uniqueName);
}

void WebRemoteFrameImpl::setReplicatedShouldEnforceStrictMixedContentChecking(bool shouldEnforce) const
{
    DCHECK(frame());
    frame()->securityContext()->setShouldEnforceStrictMixedContentChecking(shouldEnforce);
}

void WebRemoteFrameImpl::setReplicatedPotentiallyTrustworthyUniqueOrigin(bool isUniqueOriginPotentiallyTrustworthy) const
{
    DCHECK(frame());
    // If |isUniqueOriginPotentiallyTrustworthy| is true, then the origin must be unique.
    DCHECK(!isUniqueOriginPotentiallyTrustworthy || frame()->securityContext()->getSecurityOrigin()->isUnique());
    frame()->securityContext()->getSecurityOrigin()->setUniqueOriginIsPotentiallyTrustworthy(isUniqueOriginPotentiallyTrustworthy);
}

void WebRemoteFrameImpl::DispatchLoadEventForFrameOwner() const
{
    DCHECK(frame()->owner()->isLocal());
    frame()->owner()->dispatchLoad();
}

void WebRemoteFrameImpl::didStartLoading()
{
    frame()->setIsLoading(true);
}

void WebRemoteFrameImpl::didStopLoading()
{
    frame()->setIsLoading(false);
    if (parent() && parent()->isWebLocalFrame()) {
        WebLocalFrameImpl* parentFrame =
            toWebLocalFrameImpl(parent()->toWebLocalFrame());
        parentFrame->frame()->loader().checkCompleted();
    }
}

bool WebRemoteFrameImpl::isIgnoredForHitTest() const
{
    HTMLFrameOwnerElement* owner = frame()->deprecatedLocalOwner();
    if (!owner || !owner->layoutObject())
        return false;
    return owner->layoutObject()->style()->pointerEvents() == PE_NONE;
}

WebRemoteFrameImpl::WebRemoteFrameImpl(WebTreeScopeType scope, WebRemoteFrameClient* client)
    : WebRemoteFrame(scope)
    , m_frameClient(RemoteFrameClientImpl::create(this))
    , m_client(client)
#if ENABLE(OILPAN)
    , m_selfKeepAlive(this)
#endif
{
}

} // namespace blink
