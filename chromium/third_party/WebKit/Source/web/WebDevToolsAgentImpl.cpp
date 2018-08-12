/*
 * Copyright (C) 2010-2011 Google Inc. All rights reserved.
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

#include "web/WebDevToolsAgentImpl.h"

#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/V8Binding.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/inspector/InspectedFrames.h"
#include "core/inspector/InspectorAnimationAgent.h"
#include "core/inspector/InspectorApplicationCacheAgent.h"
#include "core/inspector/InspectorCSSAgent.h"
#include "core/inspector/InspectorDOMAgent.h"
#include "core/inspector/InspectorDOMDebuggerAgent.h"
#include "core/inspector/InspectorDebuggerAgent.h"
#include "core/inspector/InspectorHeapProfilerAgent.h"
#include "core/inspector/InspectorInputAgent.h"
#include "core/inspector/InspectorInstrumentation.h"
#include "core/inspector/InspectorLayerTreeAgent.h"
#include "core/inspector/InspectorMemoryAgent.h"
#include "core/inspector/InspectorPageAgent.h"
#include "core/inspector/InspectorProfilerAgent.h"
#include "core/inspector/InspectorResourceAgent.h"
#include "core/inspector/InspectorResourceContentLoader.h"
#include "core/inspector/InspectorTaskRunner.h"
#include "core/inspector/InspectorTracingAgent.h"
#include "core/inspector/InspectorWorkerAgent.h"
#include "core/inspector/InstrumentingAgents.h"
#include "core/inspector/LayoutEditor.h"
#include "core/inspector/MainThreadDebugger.h"
#include "core/inspector/PageConsoleAgent.h"
#include "core/inspector/PageDebuggerAgent.h"
#include "core/inspector/PageRuntimeAgent.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "modules/accessibility/InspectorAccessibilityAgent.h"
#include "modules/cachestorage/InspectorCacheStorageAgent.h"
#include "modules/device_orientation/DeviceOrientationInspectorAgent.h"
#include "modules/indexeddb/InspectorIndexedDBAgent.h"
#include "modules/storage/InspectorDOMStorageAgent.h"
#include "modules/webdatabase/InspectorDatabaseAgent.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/ThreadSafeFunctional.h"
#include "platform/TraceEvent.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/inspector_protocol/Dispatcher.h"
#include "platform/inspector_protocol/Frontend.h"
#include "platform/inspector_protocol/Values.h"
#include "platform/v8_inspector/public/V8Debugger.h"
#include "platform/v8_inspector/public/V8InspectorSession.h"
#include "public/platform/Platform.h"
#include "public/platform/WebLayerTreeView.h"
#include "public/platform/WebRect.h"
#include "public/platform/WebString.h"
#include "public/web/WebDevToolsAgentClient.h"
#include "public/web/WebSettings.h"
#include "web/DevToolsEmulator.h"
#include "web/InspectorEmulationAgent.h"
#include "web/InspectorOverlay.h"
#include "web/InspectorRenderingAgent.h"
#include "web/WebFrameWidgetImpl.h"
#include "web/WebInputEventConversion.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebSettingsImpl.h"
#include "web/WebViewImpl.h"
#include "wtf/MathExtras.h"
#include "wtf/Noncopyable.h"
#include "wtf/text/WTFString.h"

namespace blink {

class ClientMessageLoopAdapter : public MainThreadDebugger::ClientMessageLoop {
public:
    ~ClientMessageLoopAdapter() override
    {
        s_instance = nullptr;
    }

    static void ensureMainThreadDebuggerCreated(WebDevToolsAgentClient* client)
    {
        if (s_instance)
            return;
        OwnPtr<ClientMessageLoopAdapter> instance = adoptPtr(new ClientMessageLoopAdapter(adoptPtr(client->createClientMessageLoop())));
        s_instance = instance.get();
        MainThreadDebugger::instance()->setClientMessageLoop(instance.release());
    }

    static void webViewImplClosed(WebViewImpl* view)
    {
        if (s_instance)
            s_instance->m_frozenViews.remove(view);
    }

    static void webFrameWidgetImplClosed(WebFrameWidgetImpl* widget)
    {
        if (s_instance)
            s_instance->m_frozenWidgets.remove(widget);
    }

    static void continueProgram()
    {
        // Release render thread if necessary.
        if (s_instance)
            s_instance->quitNow();
    }

    static void pauseForCreateWindow(WebLocalFrameImpl* frame)
    {
        if (s_instance)
            s_instance->runForCreateWindow(frame);
    }

    static bool resumeForCreateWindow()
    {
        return s_instance ? s_instance->quitForCreateWindow() : false;
    }

private:
    ClientMessageLoopAdapter(PassOwnPtr<WebDevToolsAgentClient::WebKitClientMessageLoop> messageLoop)
        : m_runningForDebugBreak(false)
        , m_runningForCreateWindow(false)
        , m_messageLoop(messageLoop) { }

    void run(LocalFrame* frame) override
    {
        if (m_runningForDebugBreak)
            return;

        m_runningForDebugBreak = true;
        if (!m_runningForCreateWindow)
            runLoop(WebLocalFrameImpl::fromFrame(frame));
    }

    void runForCreateWindow(WebLocalFrameImpl* frame)
    {
        if (m_runningForCreateWindow)
            return;

        m_runningForCreateWindow = true;
        if (!m_runningForDebugBreak)
            runLoop(frame);
    }

    void runLoop(WebLocalFrameImpl* frame)
    {
        // 0. Flush pending frontend messages.
        WebDevToolsAgentImpl* agent = frame->devToolsAgentImpl();
        agent->flushPendingProtocolNotifications();

        Vector<WebViewImpl*> views;
        HeapVector<Member<WebFrameWidgetImpl>> widgets;

        // 1. Disable input events.
        const HashSet<WebViewImpl*>& viewImpls = WebViewImpl::allInstances();
        HashSet<WebViewImpl*>::const_iterator viewImplsEnd = viewImpls.end();
        for (HashSet<WebViewImpl*>::const_iterator it =  viewImpls.begin(); it != viewImplsEnd; ++it) {
            WebViewImpl* view = *it;
            m_frozenViews.add(view);
            views.append(view);
            view->setIgnoreInputEvents(true);
        }

        const WebFrameWidgetsSet& widgetImpls = WebFrameWidgetImpl::allInstances();
        WebFrameWidgetsSet::const_iterator widgetImplsEnd = widgetImpls.end();
        for (WebFrameWidgetsSet::const_iterator it =  widgetImpls.begin(); it != widgetImplsEnd; ++it) {
            WebFrameWidgetImpl* widget = *it;
            m_frozenWidgets.add(widget);
            widgets.append(widget);
            widget->setIgnoreInputEvents(true);
        }

        // 2. Notify embedder about pausing.
        agent->client()->willEnterDebugLoop();

        // 3. Disable active objects
        WebView::willEnterModalLoop();

        // 4. Process messages until quitNow is called.
        m_messageLoop->run();

        // 5. Resume active objects
        WebView::didExitModalLoop();

        // 6. Resume input events.
        for (Vector<WebViewImpl*>::iterator it = views.begin(); it != views.end(); ++it) {
            if (m_frozenViews.contains(*it)) {
                // The view was not closed during the dispatch.
                (*it)->setIgnoreInputEvents(false);
            }
        }
        for (HeapVector<Member<WebFrameWidgetImpl>>::iterator it = widgets.begin(); it != widgets.end(); ++it) {
            if (m_frozenWidgets.contains(*it)) {
                // The widget was not closed during the dispatch.
                (*it)->setIgnoreInputEvents(false);
            }
        }

        // 7. Notify embedder about resuming.
        agent->client()->didExitDebugLoop();

        // 8. All views have been resumed, clear the set.
        m_frozenViews.clear();
        m_frozenWidgets.clear();
    }

    void quitNow() override
    {
        if (m_runningForDebugBreak) {
            m_runningForDebugBreak = false;
            if (!m_runningForCreateWindow)
                m_messageLoop->quitNow();
        }
    }

    bool quitForCreateWindow()
    {
        if (m_runningForCreateWindow) {
            m_runningForCreateWindow = false;
            if (!m_runningForDebugBreak)
                m_messageLoop->quitNow();
            return true;
        }
        return false;
    }

    bool m_runningForDebugBreak;
    bool m_runningForCreateWindow;
    OwnPtr<WebDevToolsAgentClient::WebKitClientMessageLoop> m_messageLoop;
    typedef HashSet<WebViewImpl*> FrozenViewsSet;
    FrozenViewsSet m_frozenViews;
    WebFrameWidgetsSet m_frozenWidgets;
    static ClientMessageLoopAdapter* s_instance;
};

ClientMessageLoopAdapter* ClientMessageLoopAdapter::s_instance = nullptr;

// static
WebDevToolsAgentImpl* WebDevToolsAgentImpl::create(WebLocalFrameImpl* frame, WebDevToolsAgentClient* client)
{
    WebViewImpl* view = frame->viewImpl();
    // TODO(dgozman): sometimes view->mainFrameImpl() does return null, even though |frame| is meant to be main frame.
    // See http://crbug.com/526162.
    bool isMainFrame = view && !frame->parent();
    if (!isMainFrame) {
        WebDevToolsAgentImpl* agent = new WebDevToolsAgentImpl(frame, client, nullptr);
        if (frame->frameWidget())
            agent->layerTreeViewChanged(toWebFrameWidgetImpl(frame->frameWidget())->layerTreeView());
        return agent;
    }

    WebDevToolsAgentImpl* agent = new WebDevToolsAgentImpl(frame, client, InspectorOverlay::create(view));
    // TODO(dgozman): we should actually pass the view instead of frame, but during
    // remote->local transition we cannot access mainFrameImpl() yet, so we have to store the
    // frame which will become the main frame later.
    agent->m_agents.append(InspectorRenderingAgent::create(frame, agent->m_overlay.get()));
    agent->m_agents.append(InspectorEmulationAgent::create(frame, agent));
    // TODO(dgozman): migrate each of the following agents to frame once module is ready.
    agent->m_agents.append(InspectorDatabaseAgent::create(view->page()));
    agent->m_agents.append(DeviceOrientationInspectorAgent::create(view->page()));
    agent->m_agents.append(InspectorAccessibilityAgent::create(view->page()));
    agent->m_agents.append(InspectorDOMStorageAgent::create(view->page()));
    agent->m_agents.append(InspectorCacheStorageAgent::create());
    agent->layerTreeViewChanged(view->layerTreeView());
    return agent;
}

WebDevToolsAgentImpl::WebDevToolsAgentImpl(
    WebLocalFrameImpl* webLocalFrameImpl,
    WebDevToolsAgentClient* client,
    InspectorOverlay* overlay)
    : m_client(client)
    , m_webLocalFrameImpl(webLocalFrameImpl)
    , m_attached(false)
#if DCHECK_IS_ON()
    , m_hasBeenDisposed(false)
#endif
    , m_instrumentingAgents(m_webLocalFrameImpl->frame()->instrumentingAgents())
    , m_resourceContentLoader(InspectorResourceContentLoader::create(m_webLocalFrameImpl->frame()))
    , m_overlay(overlay)
    , m_inspectedFrames(InspectedFrames::create(m_webLocalFrameImpl->frame()))
    , m_domAgent(nullptr)
    , m_pageAgent(nullptr)
    , m_resourceAgent(nullptr)
    , m_layerTreeAgent(nullptr)
    , m_tracingAgent(nullptr)
    , m_agents(m_instrumentingAgents.get())
    , m_deferredAgentsInitialized(false)
    , m_sessionId(0)
    , m_stateMuted(false)
    , m_layerTreeId(0)
{
    DCHECK(isMainThread());
    DCHECK(m_webLocalFrameImpl->frame());
}

WebDevToolsAgentImpl::~WebDevToolsAgentImpl()
{
#if DCHECK_IS_ON()
    DCHECK(m_hasBeenDisposed);
#endif
}

void WebDevToolsAgentImpl::dispose()
{
    // Explicitly dispose of the agent before destructing to ensure
    // same behavior (and correctness) with and without Oilpan.
    if (m_attached)
        Platform::current()->currentThread()->removeTaskObserver(this);
#if DCHECK_IS_ON()
    DCHECK(!m_hasBeenDisposed);
    m_hasBeenDisposed = true;
#endif
}

// static
void WebDevToolsAgentImpl::webViewImplClosed(WebViewImpl* webViewImpl)
{
    ClientMessageLoopAdapter::webViewImplClosed(webViewImpl);
}

// static
void WebDevToolsAgentImpl::webFrameWidgetImplClosed(WebFrameWidgetImpl* webFrameWidgetImpl)
{
    ClientMessageLoopAdapter::webFrameWidgetImplClosed(webFrameWidgetImpl);
}

DEFINE_TRACE(WebDevToolsAgentImpl)
{
    visitor->trace(m_webLocalFrameImpl);
    visitor->trace(m_instrumentingAgents);
    visitor->trace(m_resourceContentLoader);
    visitor->trace(m_overlay);
    visitor->trace(m_inspectedFrames);
    visitor->trace(m_domAgent);
    visitor->trace(m_pageAgent);
    visitor->trace(m_resourceAgent);
    visitor->trace(m_layerTreeAgent);
    visitor->trace(m_tracingAgent);
    visitor->trace(m_agents);
}

void WebDevToolsAgentImpl::willBeDestroyed()
{
    DCHECK(m_webLocalFrameImpl->frame());
    DCHECK(m_inspectedFrames->root()->view());

    detach();
    m_resourceContentLoader->dispose();
    m_agents.discardAgents();
    m_instrumentingAgents->reset();
    m_v8Session.clear();
}

void WebDevToolsAgentImpl::initializeDeferredAgents()
{
    if (m_deferredAgentsInitialized)
        return;
    m_deferredAgentsInitialized = true;

    ClientMessageLoopAdapter::ensureMainThreadDebuggerCreated(m_client);
    MainThreadDebugger* mainThreadDebugger = MainThreadDebugger::instance();
    v8::Isolate* isolate = V8PerIsolateData::mainThreadIsolate();

    m_v8Session = mainThreadDebugger->debugger()->connect(mainThreadDebugger->contextGroupId(m_inspectedFrames->root()));
    V8RuntimeAgent* runtimeAgent = m_v8Session->runtimeAgent();

    m_agents.append(PageRuntimeAgent::create(this, runtimeAgent, m_inspectedFrames.get()));

    InspectorDOMAgent* domAgent = InspectorDOMAgent::create(isolate, m_inspectedFrames.get(), runtimeAgent, m_overlay.get());
    m_domAgent = domAgent;
    m_agents.append(domAgent);

    InspectorLayerTreeAgent* layerTreeAgent = InspectorLayerTreeAgent::create(m_inspectedFrames.get());
    m_layerTreeAgent = layerTreeAgent;
    m_agents.append(layerTreeAgent);

    InspectorResourceAgent* resourceAgent = InspectorResourceAgent::create(m_inspectedFrames.get());
    m_resourceAgent = resourceAgent;
    m_agents.append(resourceAgent);

    InspectorCSSAgent* cssAgent = InspectorCSSAgent::create(m_domAgent, m_inspectedFrames.get(), m_resourceAgent, m_resourceContentLoader.get());
    m_agents.append(cssAgent);

    m_agents.append(InspectorAnimationAgent::create(m_inspectedFrames.get(), m_domAgent, cssAgent, runtimeAgent));

    m_agents.append(InspectorMemoryAgent::create());

    m_agents.append(InspectorApplicationCacheAgent::create(m_inspectedFrames.get()));

    m_agents.append(InspectorIndexedDBAgent::create(m_inspectedFrames.get()));

    InspectorDebuggerAgent* debuggerAgent = PageDebuggerAgent::create(m_v8Session->debuggerAgent(), m_inspectedFrames.get());
    m_agents.append(debuggerAgent);

    PageConsoleAgent* pageConsoleAgent = PageConsoleAgent::create(runtimeAgent, m_v8Session->debuggerAgent(), m_domAgent, m_inspectedFrames.get());
    m_agents.append(pageConsoleAgent);

    InspectorWorkerAgent* workerAgent = InspectorWorkerAgent::create(m_inspectedFrames.get(), pageConsoleAgent);
    m_agents.append(workerAgent);

    InspectorTracingAgent* tracingAgent = InspectorTracingAgent::create(this, workerAgent, m_inspectedFrames.get());
    m_tracingAgent = tracingAgent;
    m_agents.append(tracingAgent);

    m_agents.append(InspectorDOMDebuggerAgent::create(isolate, m_domAgent, runtimeAgent, debuggerAgent->v8Agent()));

    m_agents.append(InspectorInputAgent::create(m_inspectedFrames.get()));

    m_agents.append(InspectorProfilerAgent::create(m_v8Session->profilerAgent(), m_overlay.get()));

    m_agents.append(InspectorHeapProfilerAgent::create(isolate, m_v8Session->heapProfilerAgent()));

    InspectorPageAgent* pageAgent = InspectorPageAgent::create(m_inspectedFrames.get(), this, m_resourceContentLoader.get(), debuggerAgent);
    m_pageAgent = pageAgent;
    m_agents.append(pageAgent);

    runtimeAgent->setClearConsoleCallback(bind<>(&InspectorConsoleAgent::clearAllMessages, pageConsoleAgent));
    m_tracingAgent->setLayerTreeId(m_layerTreeId);
    if (m_overlay)
        m_overlay->init(cssAgent, debuggerAgent, m_domAgent);
}

void WebDevToolsAgentImpl::attach(const WebString& hostId, int sessionId)
{
    if (m_attached)
        return;

    // Set the attached bit first so that sync notifications were delivered.
    m_attached = true;
    m_sessionId = sessionId;

    initializeDeferredAgents();
    m_resourceAgent->setHostId(hostId);

    m_inspectorFrontend = adoptPtr(new protocol::Frontend(this));
    // We can reconnect to existing front-end -> unmute state.
    m_stateMuted = false;
    m_agents.setFrontend(m_inspectorFrontend.get());

    InspectorInstrumentation::registerInstrumentingAgents(m_instrumentingAgents.get());
    InspectorInstrumentation::frontendCreated();

    m_inspectorBackendDispatcher = protocol::Dispatcher::create(this);
    m_agents.registerInDispatcher(m_inspectorBackendDispatcher.get());

    Platform::current()->currentThread()->addTaskObserver(this);
}

void WebDevToolsAgentImpl::reattach(const WebString& hostId, int sessionId, const WebString& savedState)
{
    if (m_attached)
        return;

    attach(hostId, sessionId);
    m_agents.restore(savedState);
}

void WebDevToolsAgentImpl::detach()
{
    if (!m_attached)
        return;

    Platform::current()->currentThread()->removeTaskObserver(this);

    m_inspectorBackendDispatcher->clearFrontend();
    m_inspectorBackendDispatcher.clear();

    // Destroying agents would change the state, but we don't want that.
    // Pre-disconnect state will be used to restore inspector agents.
    m_stateMuted = true;
    m_agents.clearFrontend();
    m_inspectorFrontend.clear();

    // Release overlay resources.
    if (m_overlay)
        m_overlay->clear();
    InspectorInstrumentation::frontendDeleted();
    InspectorInstrumentation::unregisterInstrumentingAgents(m_instrumentingAgents.get());

    m_sessionId = 0;
    m_attached = false;
}

void WebDevToolsAgentImpl::continueProgram()
{
    ClientMessageLoopAdapter::continueProgram();
}

void WebDevToolsAgentImpl::didCommitLoadForLocalFrame(LocalFrame* frame)
{
    m_resourceContentLoader->didCommitLoadForLocalFrame(frame);
    m_agents.didCommitLoadForLocalFrame(frame);
}

bool WebDevToolsAgentImpl::screencastEnabled()
{
    return m_pageAgent && m_pageAgent->screencastEnabled();
}

void WebDevToolsAgentImpl::willAddPageOverlay(const GraphicsLayer* layer)
{
    if (m_layerTreeAgent)
        m_layerTreeAgent->willAddPageOverlay(layer);
}

void WebDevToolsAgentImpl::didRemovePageOverlay(const GraphicsLayer* layer)
{
    if (m_layerTreeAgent)
        m_layerTreeAgent->didRemovePageOverlay(layer);
}

void WebDevToolsAgentImpl::layerTreeViewChanged(WebLayerTreeView* layerTreeView)
{
    m_layerTreeId = layerTreeView ? layerTreeView->layerTreeId() : 0;
    if (m_tracingAgent)
        m_tracingAgent->setLayerTreeId(m_layerTreeId);
}

void WebDevToolsAgentImpl::enableTracing(const String& categoryFilter)
{
    m_client->enableTracing(categoryFilter);
}

void WebDevToolsAgentImpl::disableTracing()
{
    m_client->disableTracing();
}

void WebDevToolsAgentImpl::setCPUThrottlingRate(double rate)
{
    m_client->setCPUThrottlingRate(rate);
}

void WebDevToolsAgentImpl::dispatchOnInspectorBackend(int sessionId, const WebString& message)
{
    if (!m_attached)
        return;
    if (WebDevToolsAgent::shouldInterruptForMessage(message))
        MainThreadDebugger::instance()->taskRunner()->runAllTasksDontWait();
    else
        dispatchMessageFromFrontend(sessionId, message);
}

void WebDevToolsAgentImpl::dispatchMessageFromFrontend(int sessionId, const String& message)
{
    InspectorTaskRunner::IgnoreInterruptsScope scope(MainThreadDebugger::instance()->taskRunner());
    if (m_inspectorBackendDispatcher)
        m_inspectorBackendDispatcher->dispatch(sessionId, message);
}

void WebDevToolsAgentImpl::inspectElementAt(const WebPoint& pointInRootFrame)
{
    if (!m_domAgent)
        return;
    HitTestRequest::HitTestRequestType hitType = HitTestRequest::Move | HitTestRequest::ReadOnly | HitTestRequest::AllowChildFrameContent;
    HitTestRequest request(hitType);
    WebMouseEvent dummyEvent;
    dummyEvent.type = WebInputEvent::MouseDown;
    dummyEvent.x = pointInRootFrame.x;
    dummyEvent.y = pointInRootFrame.y;
    IntPoint transformedPoint = PlatformMouseEventBuilder(m_webLocalFrameImpl->frameView(), dummyEvent).position();
    HitTestResult result(request, m_webLocalFrameImpl->frameView()->rootFrameToContents(transformedPoint));
    m_webLocalFrameImpl->frame()->contentLayoutItem().hitTest(result);
    Node* node = result.innerNode();
    if (!node && m_webLocalFrameImpl->frame()->document())
        node = m_webLocalFrameImpl->frame()->document()->documentElement();
    m_domAgent->inspect(node);
}

void WebDevToolsAgentImpl::failedToRequestDevTools()
{
    ClientMessageLoopAdapter::resumeForCreateWindow();
}

void WebDevToolsAgentImpl::sendProtocolResponse(int sessionId, int callId, PassOwnPtr<protocol::DictionaryValue> message)
{
    if (!m_attached)
        return;
    flushPendingProtocolNotifications();
    String stateToSend;
    if (!m_stateMuted) {
        stateToSend = m_agents.state();
        if (stateToSend == m_stateCookie)
            stateToSend = String();
        else
            m_stateCookie = stateToSend;
    }

    m_client->sendProtocolMessage(sessionId, callId, message->toJSONString(), stateToSend);
}

void WebDevToolsAgentImpl::sendProtocolNotification(PassOwnPtr<protocol::DictionaryValue> message)
{
    if (!m_attached)
        return;
    m_notificationQueue.append(std::make_pair(m_sessionId, message));
}

void WebDevToolsAgentImpl::flush()
{
    flushPendingProtocolNotifications();
}

void WebDevToolsAgentImpl::resumeStartup()
{
    // If we've paused for createWindow, handle it ourselves.
    if (ClientMessageLoopAdapter::resumeForCreateWindow())
        return;
    // Otherwise, pass to the client (embedded workers do it differently).
    m_client->resumeStartup();
}

void WebDevToolsAgentImpl::pageLayoutInvalidated(bool resized)
{
    if (m_overlay)
        m_overlay->pageLayoutInvalidated(resized);
}

void WebDevToolsAgentImpl::setPausedInDebuggerMessage(const String& message)
{
    if (m_overlay)
        m_overlay->setPausedInDebuggerMessage(message);
}

void WebDevToolsAgentImpl::waitForCreateWindow(LocalFrame* frame)
{
    if (!m_attached)
        return;
    if (m_client->requestDevToolsForFrame(WebLocalFrameImpl::fromFrame(frame)))
        ClientMessageLoopAdapter::pauseForCreateWindow(m_webLocalFrameImpl);
}

WebString WebDevToolsAgentImpl::evaluateInWebInspectorOverlay(const WebString& script)
{
    if (!m_overlay)
        return WebString();

    return m_overlay->evaluateInOverlayForTest(script);
}

void WebDevToolsAgentImpl::flushPendingProtocolNotifications()
{
    if (m_attached) {
        m_agents.flushPendingProtocolNotifications();
        for (size_t i = 0; i < m_notificationQueue.size(); ++i)
            m_client->sendProtocolMessage(m_notificationQueue[i].first, 0, m_notificationQueue[i].second->toJSONString(), WebString());
    }
    m_notificationQueue.clear();
}

void WebDevToolsAgentImpl::willProcessTask()
{
    if (!m_attached)
        return;
    if (InspectorProfilerAgent* profilerAgent = m_instrumentingAgents->inspectorProfilerAgent())
        profilerAgent->willProcessTask();
}

void WebDevToolsAgentImpl::didProcessTask()
{
    if (!m_attached)
        return;
    if (InspectorProfilerAgent* profilerAgent = m_instrumentingAgents->inspectorProfilerAgent())
        profilerAgent->didProcessTask();
    flushPendingProtocolNotifications();
}

void WebDevToolsAgentImpl::runDebuggerTask(int sessionId, PassOwnPtr<WebDevToolsAgent::MessageDescriptor> descriptor)
{
    WebDevToolsAgent* webagent = descriptor->agent();
    if (!webagent)
        return;

    WebDevToolsAgentImpl* agentImpl = static_cast<WebDevToolsAgentImpl*>(webagent);
    if (agentImpl->m_attached)
        agentImpl->dispatchMessageFromFrontend(sessionId, descriptor->message());
}

void WebDevToolsAgent::interruptAndDispatch(int sessionId, MessageDescriptor* rawDescriptor)
{
    // rawDescriptor can't be a PassOwnPtr because interruptAndDispatch is a WebKit API function.
    MainThreadDebugger::interruptMainThreadAndRun(threadSafeBind(WebDevToolsAgentImpl::runDebuggerTask, sessionId, adoptPtr(rawDescriptor)));
}

bool WebDevToolsAgent::shouldInterruptForMessage(const WebString& message)
{
    String16 commandName;
    if (!protocol::Dispatcher::getCommandName(message, &commandName))
        return false;
    return commandName == "Debugger.pause"
        || commandName == "Debugger.setBreakpoint"
        || commandName == "Debugger.setBreakpointByUrl"
        || commandName == "Debugger.removeBreakpoint"
        || commandName == "Debugger.setBreakpointsActive";
}

} // namespace blink
