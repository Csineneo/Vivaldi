// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/compositorworker/CompositorWorkerThread.h"

#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/V8GCController.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/testing/DummyPageHolder.h"
#include "core/workers/WorkerLoaderProxy.h"
#include "core/workers/WorkerObjectProxy.h"
#include "core/workers/WorkerThreadStartupData.h"
#include "platform/ThreadSafeFunctional.h"
#include "platform/WaitableEvent.h"
#include "platform/heap/Handle.h"
#include "platform/testing/TestingPlatformSupport.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebAddressSpace.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

class TestCompositorWorkerThread : public CompositorWorkerThread {
public:
    TestCompositorWorkerThread(WorkerLoaderProxyProvider* loaderProxyProvider, WorkerObjectProxy& objectProxy, double timeOrigin, WaitableEvent* startEvent)
        : CompositorWorkerThread(WorkerLoaderProxy::create(loaderProxyProvider), objectProxy, timeOrigin)
        , m_startEvent(startEvent)
    {
    }

    ~TestCompositorWorkerThread() override {}

    void setCallbackAfterV8Termination(PassOwnPtr<Function<void()>> callback)
    {
        m_v8TerminationCallback = callback;
    }

private:
    // WorkerThread:
    void didStartWorkerThread() override
    {
        m_startEvent->signal();
    }

    void terminateV8Execution() override
    {
        // This could be called on worker thread, but not in the test.
        ASSERT(isMainThread());
        CompositorWorkerThread::terminateV8Execution();
        if (m_v8TerminationCallback)
            (*m_v8TerminationCallback)();
    }

    void willDestroyIsolate() override
    {
        v8::Isolate::GetCurrent()->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
        Heap::collectAllGarbage();
        CompositorWorkerThread::willDestroyIsolate();
    }

    WaitableEvent* m_startEvent;
    OwnPtr<Function<void()>> m_v8TerminationCallback;
};

// A null WorkerObjectProxy, supplied when creating CompositorWorkerThreads.
class TestCompositorWorkerObjectProxy : public WorkerObjectProxy {
public:
    static PassOwnPtr<TestCompositorWorkerObjectProxy> create(ExecutionContext* context)
    {
        return adoptPtr(new TestCompositorWorkerObjectProxy(context));
    }

    // (Empty) WorkerReportingProxy implementation:
    virtual void reportException(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, int exceptionId) {}
    void reportConsoleMessage(ConsoleMessage*) override {}
    void postMessageToPageInspector(const String&) override {}
    void postWorkerConsoleAgentEnabled() override {}

    void didEvaluateWorkerScript(bool success) override {}
    void workerGlobalScopeStarted(WorkerGlobalScope*) override {}
    void workerGlobalScopeClosed() override {}
    void workerThreadTerminated() override {}
    void willDestroyWorkerGlobalScope() override {}

    ExecutionContext* getExecutionContext() override { return m_executionContext.get(); }

private:
    TestCompositorWorkerObjectProxy(ExecutionContext* context)
        : WorkerObjectProxy(nullptr)
        , m_executionContext(context)
    {
    }

    Persistent<ExecutionContext> m_executionContext;
};

class CompositorWorkerTestPlatform : public TestingPlatformSupport {
public:
    CompositorWorkerTestPlatform()
        : m_thread(adoptPtr(m_oldPlatform->createThread("Compositor")))
    {
    }

    WebThread* compositorThread() const override
    {
        return m_thread.get();
    }

    WebCompositorSupport* compositorSupport() override { return &m_compositorSupport; }

private:
    OwnPtr<WebThread> m_thread;
    TestingCompositorSupport m_compositorSupport;
};

} // namespace

class CompositorWorkerThreadTest : public ::testing::Test {
public:
    void SetUp() override
    {
        m_page = DummyPageHolder::create();
        m_objectProxy = TestCompositorWorkerObjectProxy::create(&m_page->document());
        m_securityOrigin = SecurityOrigin::create(KURL(ParsedURLString, "http://fake.url/"));
    }

    void TearDown() override
    {
        ASSERT(!hasThread());
        ASSERT(!hasIsolate());
        m_page.clear();
    }

    PassOwnPtr<TestCompositorWorkerThread> createCompositorWorker(WaitableEvent* startEvent)
    {
        TestCompositorWorkerThread* workerThread = new TestCompositorWorkerThread(nullptr, *m_objectProxy, 0, startEvent);
        WorkerClients* clients = nullptr;
        workerThread->start(WorkerThreadStartupData::create(
            KURL(ParsedURLString, "http://fake.url/"),
            "fake user agent",
            "//fake source code",
            nullptr,
            DontPauseWorkerGlobalScopeOnStart,
            adoptPtr(new Vector<CSPHeaderAndType>()),
            m_securityOrigin.get(),
            clients,
            WebAddressSpaceLocal,
            V8CacheOptionsDefault));
        return adoptPtr(workerThread);
    }

    void createWorkerAdapter(OwnPtr<CompositorWorkerThread>* workerThread, WaitableEvent* creationEvent)
    {
        *workerThread = createCompositorWorker(creationEvent);
    }

    // Attempts to run some simple script for |worker|.
    void checkWorkerCanExecuteScript(WorkerThread* worker)
    {
        OwnPtr<WaitableEvent> waitEvent = adoptPtr(new WaitableEvent());
        worker->backingThread().platformThread().getWebTaskRunner()->postTask(BLINK_FROM_HERE, threadSafeBind(&CompositorWorkerThreadTest::executeScriptInWorker, AllowCrossThreadAccess(this),
            AllowCrossThreadAccess(worker), AllowCrossThreadAccess(waitEvent.get())));
        waitEvent->wait();
    }

    void waitForWaitableEventAfterIteratingCurrentLoop(WaitableEvent* waitEvent)
    {
        testing::runPendingTasks();
        waitEvent->wait();
    }

    bool hasThread() const
    {
        return CompositorWorkerThread::hasThreadForTest();
    }

    bool hasIsolate() const
    {
        return CompositorWorkerThread::hasIsolateForTest();
    }

private:
    void executeScriptInWorker(WorkerThread* worker, WaitableEvent* waitEvent)
    {
        WorkerOrWorkletScriptController* scriptController = worker->workerGlobalScope()->scriptController();
        bool evaluateResult = scriptController->evaluate(ScriptSourceCode("var counter = 0; ++counter;"));
        ASSERT_UNUSED(evaluateResult, evaluateResult);
        waitEvent->signal();
    }

    OwnPtr<DummyPageHolder> m_page;
    RefPtr<SecurityOrigin> m_securityOrigin;
    OwnPtr<WorkerObjectProxy> m_objectProxy;
    CompositorWorkerTestPlatform m_testPlatform;
};

TEST_F(CompositorWorkerThreadTest, Basic)
{
    OwnPtr<WaitableEvent> creationEvent = adoptPtr(new WaitableEvent());
    OwnPtr<CompositorWorkerThread> compositorWorker = createCompositorWorker(creationEvent.get());
    waitForWaitableEventAfterIteratingCurrentLoop(creationEvent.get());
    checkWorkerCanExecuteScript(compositorWorker.get());
    compositorWorker->terminateAndWait();
}

// Tests that the same WebThread is used for new workers if the WebThread is still alive.
TEST_F(CompositorWorkerThreadTest, CreateSecondAndTerminateFirst)
{
    // Create the first worker and wait until it is initialized.
    OwnPtr<WaitableEvent> firstCreationEvent = adoptPtr(new WaitableEvent());
    OwnPtr<CompositorWorkerThread> firstWorker = createCompositorWorker(firstCreationEvent.get());
    WebThreadSupportingGC* firstThread = CompositorWorkerThread::sharedBackingThread();
    ASSERT(firstThread);
    waitForWaitableEventAfterIteratingCurrentLoop(firstCreationEvent.get());
    v8::Isolate* firstIsolate = firstWorker->isolate();
    ASSERT(firstIsolate);

    // Create the second worker and immediately destroy the first worker.
    OwnPtr<WaitableEvent> secondCreationEvent = adoptPtr(new WaitableEvent());
    OwnPtr<CompositorWorkerThread> secondWorker = createCompositorWorker(secondCreationEvent.get());
    firstWorker->terminateAndWait();

    // Wait until the second worker is initialized. Verify that the second worker is using the same
    // thread and Isolate as the first worker.
    WebThreadSupportingGC* secondThread = CompositorWorkerThread::sharedBackingThread();
    ASSERT(secondThread);
    waitForWaitableEventAfterIteratingCurrentLoop(secondCreationEvent.get());
    EXPECT_EQ(firstThread, secondThread);

    v8::Isolate* secondIsolate = secondWorker->isolate();
    ASSERT(secondIsolate);
    EXPECT_EQ(firstIsolate, secondIsolate);

    // Verify that the worker can still successfully execute script.
    checkWorkerCanExecuteScript(secondWorker.get());

    secondWorker->terminateAndWait();
}

static void checkCurrentIsolate(v8::Isolate* isolate, WaitableEvent* event)
{
    EXPECT_EQ(v8::Isolate::GetCurrent(), isolate);
    event->signal();
}

// Tests that a new WebThread is created if all existing workers are terminated before a new worker is created.
TEST_F(CompositorWorkerThreadTest, TerminateFirstAndCreateSecond)
{
    // Create the first worker, wait until it is initialized, and terminate it.
    OwnPtr<WaitableEvent> creationEvent = adoptPtr(new WaitableEvent());
    OwnPtr<CompositorWorkerThread> compositorWorker = createCompositorWorker(creationEvent.get());
    WebThreadSupportingGC* firstThread = CompositorWorkerThread::sharedBackingThread();
    waitForWaitableEventAfterIteratingCurrentLoop(creationEvent.get());
    ASSERT(compositorWorker->isolate());
    compositorWorker->terminateAndWait();

    // Create the second worker. Verify that the second worker lives in a different WebThread since the first
    // thread will have been destroyed after destroying the first worker.
    creationEvent = adoptPtr(new WaitableEvent());
    compositorWorker = createCompositorWorker(creationEvent.get());
    WebThreadSupportingGC* secondThread = CompositorWorkerThread::sharedBackingThread();
    EXPECT_NE(firstThread, secondThread);
    waitForWaitableEventAfterIteratingCurrentLoop(creationEvent.get());

    // Jump over to the worker's thread to verify that the Isolate is set up correctly and execute script.
    OwnPtr<WaitableEvent> checkEvent = adoptPtr(new WaitableEvent());
    secondThread->platformThread().getWebTaskRunner()->postTask(BLINK_FROM_HERE, threadSafeBind(&checkCurrentIsolate, AllowCrossThreadAccess(compositorWorker->isolate()), AllowCrossThreadAccess(checkEvent.get())));
    waitForWaitableEventAfterIteratingCurrentLoop(checkEvent.get());
    checkWorkerCanExecuteScript(compositorWorker.get());

    compositorWorker->terminateAndWait();
}

// Tests that v8::Isolate and WebThread are correctly set-up if a worker is created while another is terminating.
TEST_F(CompositorWorkerThreadTest, CreatingSecondDuringTerminationOfFirst)
{
    OwnPtr<WaitableEvent> firstCreationEvent = adoptPtr(new WaitableEvent());
    OwnPtr<TestCompositorWorkerThread> firstWorker = createCompositorWorker(firstCreationEvent.get());
    waitForWaitableEventAfterIteratingCurrentLoop(firstCreationEvent.get());
    v8::Isolate* firstIsolate = firstWorker->isolate();
    ASSERT(firstIsolate);

    // Request termination of the first worker, and set-up to make sure the second worker is created right as
    // the first worker terminates its isolate.
    OwnPtr<WaitableEvent> secondCreationEvent = adoptPtr(new WaitableEvent());
    OwnPtr<CompositorWorkerThread> secondWorker;
    firstWorker->setCallbackAfterV8Termination(bind(&CompositorWorkerThreadTest::createWorkerAdapter, this, &secondWorker, secondCreationEvent.get()));
    firstWorker->terminateAndWait();
    ASSERT(secondWorker);

    waitForWaitableEventAfterIteratingCurrentLoop(secondCreationEvent.get());
    v8::Isolate* secondIsolate = secondWorker->isolate();
    ASSERT(secondIsolate);
    EXPECT_EQ(firstIsolate, secondIsolate);

    // Verify that the isolate can run some scripts correctly in the second worker.
    checkWorkerCanExecuteScript(secondWorker.get());
    secondWorker->terminateAndWait();
}

} // namespace blink
