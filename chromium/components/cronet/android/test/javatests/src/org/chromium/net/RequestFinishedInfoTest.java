// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import static org.chromium.base.CollectionUtil.newHashSet;

import android.os.ConditionVariable;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.net.MetricsTestUtil.TestExecutor;
import org.chromium.net.MetricsTestUtil.TestRequestFinishedListener;
import org.chromium.net.impl.CronetMetrics;
import org.chromium.net.test.EmbeddedTestServer;

import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.concurrent.Executor;

/**
 * Test RequestFinishedInfo.Listener and the metrics information it provides.
 */
public class RequestFinishedInfoTest extends CronetTestBase {
    CronetTestFramework mTestFramework;
    private EmbeddedTestServer mTestServer;
    private String mUrl;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mTestServer = EmbeddedTestServer.createAndStartDefaultServer(getContext());
        mUrl = mTestServer.getURL("/echo?status=200");
    }

    @Override
    protected void tearDown() throws Exception {
        mTestServer.stopAndDestroyServer();
        super.tearDown();
    }

    static class DirectExecutor implements Executor {
        private ConditionVariable mBlock = new ConditionVariable();

        @Override
        public void execute(Runnable task) {
            task.run();
            mBlock.open();
        }

        public void blockUntilDone() {
            mBlock.block();
        }
    }

    static class ThreadExecutor implements Executor {
        private List<Thread> mThreads = new ArrayList<Thread>();

        @Override
        public void execute(Runnable task) {
            Thread newThread = new Thread(task);
            mThreads.add(newThread);
            newThread.start();
        }

        public void joinAll() throws InterruptedException {
            for (Thread thread : mThreads) {
                thread.join();
            }
        }
    }

    @SmallTest
    @OnlyRunNativeCronet
    @Feature({"Cronet"})
    @SuppressWarnings("deprecation")
    public void testRequestFinishedListener() throws Exception {
        mTestFramework = startCronetTestFramework();
        TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
        mTestFramework.mCronetEngine.addRequestFinishedListener(requestFinishedListener);
        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder urlRequestBuilder = new UrlRequest.Builder(
                mUrl, callback, callback.getExecutor(), mTestFramework.mCronetEngine);
        Date startTime = new Date();
        urlRequestBuilder.addRequestAnnotation("request annotation")
                .addRequestAnnotation(this)
                .build()
                .start();
        callback.blockForDone();
        requestFinishedListener.blockUntilDone();
        Date endTime = new Date();

        RequestFinishedInfo requestInfo = requestFinishedListener.getRequestInfo();
        assertNotNull("RequestFinishedInfo.Listener must be called", requestInfo);
        assertEquals(mUrl, requestInfo.getUrl());
        assertNotNull(requestInfo.getResponseInfo());
        assertEquals(newHashSet("request annotation", this), // Use sets for unordered comparison.
                new HashSet<Object>(requestInfo.getAnnotations()));
        assertEquals(RequestFinishedInfo.SUCCEEDED, requestInfo.getFinishedReason());
        assertNull(requestInfo.getException());
        RequestFinishedInfo.Metrics metrics = requestInfo.getMetrics();
        assertNotNull("RequestFinishedInfo.getMetrics() must not be null", metrics);
        // Check old (deprecated) timing metrics
        assertTrue(metrics.getTotalTimeMs() > 0);
        assertTrue(metrics.getTotalTimeMs() >= metrics.getTtfbMs());
        // Check new timing metrics
        MetricsTestUtil.checkTimingMetrics(metrics, startTime, endTime);
        MetricsTestUtil.checkHasConnectTiming(metrics, startTime, endTime, false);
        assertNull(metrics.getPushStart());
        assertNull(metrics.getPushEnd());
        // Check data use metrics
        assertTrue(metrics.getSentBytesCount() > 0);
        assertTrue(metrics.getReceivedBytesCount() > 0);
        mTestFramework.mCronetEngine.shutdown();
    }

    @SmallTest
    @OnlyRunNativeCronet
    @Feature({"Cronet"})
    @SuppressWarnings("deprecation")
    public void testRequestFinishedListenerDirectExecutor() throws Exception {
        mTestFramework = startCronetTestFramework();
        DirectExecutor testExecutor = new DirectExecutor();
        TestRequestFinishedListener requestFinishedListener =
                new TestRequestFinishedListener(testExecutor);
        mTestFramework.mCronetEngine.addRequestFinishedListener(requestFinishedListener);
        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder urlRequestBuilder = new UrlRequest.Builder(
                mUrl, callback, callback.getExecutor(), mTestFramework.mCronetEngine);
        Date startTime = new Date();
        urlRequestBuilder.addRequestAnnotation("request annotation")
                .addRequestAnnotation(this)
                .build()
                .start();
        callback.blockForDone();
        // Block on the executor, not the listener, since blocking on the listener doesn't work when
        // it's created with a non-default executor.
        testExecutor.blockUntilDone();
        Date endTime = new Date();

        RequestFinishedInfo requestInfo = requestFinishedListener.getRequestInfo();
        assertNotNull("RequestFinishedInfo.Listener must be called", requestInfo);
        assertEquals(mUrl, requestInfo.getUrl());
        assertNotNull(requestInfo.getResponseInfo());
        assertEquals(newHashSet("request annotation", this), // Use sets for unordered comparison.
                new HashSet<Object>(requestInfo.getAnnotations()));
        assertEquals(RequestFinishedInfo.SUCCEEDED, requestInfo.getFinishedReason());
        assertNull(requestInfo.getException());
        RequestFinishedInfo.Metrics metrics = requestInfo.getMetrics();
        assertNotNull("RequestFinishedInfo.getMetrics() must not be null", metrics);
        assertTrue(metrics.getTotalTimeMs() > 0);
        assertTrue(metrics.getTotalTimeMs() >= metrics.getTtfbMs());
        MetricsTestUtil.checkTimingMetrics(metrics, startTime, endTime);
        MetricsTestUtil.checkHasConnectTiming(metrics, startTime, endTime, false);
        assertTrue(metrics.getSentBytesCount() > 0);
        assertTrue(metrics.getReceivedBytesCount() > 0);
        mTestFramework.mCronetEngine.shutdown();
    }

    @SmallTest
    @OnlyRunNativeCronet
    @Feature({"Cronet"})
    @SuppressWarnings("deprecation")
    public void testRequestFinishedListenerDifferentThreads() throws Exception {
        mTestFramework = startCronetTestFramework();
        TestRequestFinishedListener firstListener = new TestRequestFinishedListener();
        TestRequestFinishedListener secondListener = new TestRequestFinishedListener();
        mTestFramework.mCronetEngine.addRequestFinishedListener(firstListener);
        mTestFramework.mCronetEngine.addRequestFinishedListener(secondListener);
        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder urlRequestBuilder = new UrlRequest.Builder(
                mUrl, callback, callback.getExecutor(), mTestFramework.mCronetEngine);
        Date startTime = new Date();
        urlRequestBuilder.addRequestAnnotation("request annotation")
                .addRequestAnnotation(this)
                .build()
                .start();
        callback.blockForDone();
        firstListener.blockUntilDone();
        secondListener.blockUntilDone();
        Date endTime = new Date();

        RequestFinishedInfo firstRequestInfo = firstListener.getRequestInfo();
        RequestFinishedInfo secondRequestInfo = secondListener.getRequestInfo();
        assertNotNull("First RequestFinishedInfo.Listener must be called", firstRequestInfo);
        assertNotNull("Second RequestFinishedInfo.Listener must be called", secondRequestInfo);
        assertEquals(mUrl, firstRequestInfo.getUrl());
        assertEquals(mUrl, secondRequestInfo.getUrl());
        assertNotNull(firstRequestInfo.getResponseInfo());
        assertNotNull(secondRequestInfo.getResponseInfo());
        assertEquals(newHashSet("request annotation", this), // Use sets for unordered comparison.
                new HashSet<Object>(firstRequestInfo.getAnnotations()));
        assertEquals(newHashSet("request annotation", this),
                new HashSet<Object>(secondRequestInfo.getAnnotations()));
        assertEquals(RequestFinishedInfo.SUCCEEDED, firstRequestInfo.getFinishedReason());
        assertEquals(RequestFinishedInfo.SUCCEEDED, secondRequestInfo.getFinishedReason());
        assertNull(firstRequestInfo.getException());
        assertNull(secondRequestInfo.getException());
        RequestFinishedInfo.Metrics firstMetrics = firstRequestInfo.getMetrics();
        assertNotNull("RequestFinishedInfo.getMetrics() must not be null", firstMetrics);
        assertTrue(firstMetrics.getTotalTimeMs() > 0);
        assertTrue(firstMetrics.getTotalTimeMs() >= firstMetrics.getTtfbMs());
        MetricsTestUtil.checkTimingMetrics(firstMetrics, startTime, endTime);
        MetricsTestUtil.checkHasConnectTiming(firstMetrics, startTime, endTime, false);
        assertTrue(firstMetrics.getSentBytesCount() > 0);
        assertTrue(firstMetrics.getReceivedBytesCount() > 0);
        RequestFinishedInfo.Metrics secondMetrics = secondRequestInfo.getMetrics();
        assertNotNull("RequestFinishedInfo.getMetrics() must not be null", secondMetrics);
        assertTrue(secondMetrics.getTotalTimeMs() > 0);
        assertTrue(secondMetrics.getTotalTimeMs() >= secondMetrics.getTtfbMs());
        MetricsTestUtil.checkTimingMetrics(secondMetrics, startTime, endTime);
        MetricsTestUtil.checkHasConnectTiming(secondMetrics, startTime, endTime, false);
        assertTrue(secondMetrics.getSentBytesCount() > 0);
        assertTrue(secondMetrics.getReceivedBytesCount() > 0);
        mTestFramework.mCronetEngine.shutdown();
    }

    @SmallTest
    @OnlyRunNativeCronet
    @Feature({"Cronet"})
    @SuppressWarnings("deprecation")
    public void testRequestFinishedListenerFailedRequest() throws Exception {
        String connectionRefusedUrl = "http://127.0.0.1:3";
        mTestFramework = startCronetTestFramework();
        TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
        mTestFramework.mCronetEngine.addRequestFinishedListener(requestFinishedListener);
        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder urlRequestBuilder = new UrlRequest.Builder(connectionRefusedUrl,
                callback, callback.getExecutor(), mTestFramework.mCronetEngine);
        Date startTime = new Date();
        urlRequestBuilder.build().start();
        callback.blockForDone();
        assertTrue(callback.mOnErrorCalled);
        requestFinishedListener.blockUntilDone();
        Date endTime = new Date();

        RequestFinishedInfo requestInfo = requestFinishedListener.getRequestInfo();
        assertNotNull("RequestFinishedInfo.Listener must be called", requestInfo);
        assertEquals(connectionRefusedUrl, requestInfo.getUrl());
        assertTrue(requestInfo.getAnnotations().isEmpty());
        assertEquals(RequestFinishedInfo.FAILED, requestInfo.getFinishedReason());
        assertNotNull(requestInfo.getException());
        assertEquals(UrlRequestException.ERROR_CONNECTION_REFUSED,
                requestInfo.getException().getErrorCode());
        RequestFinishedInfo.Metrics metrics = requestInfo.getMetrics();
        assertNotNull("RequestFinishedInfo.getMetrics() must not be null", metrics);
        // The failure is occasionally fast enough that time reported is 0, so just check for null
        assertNotNull(metrics.getTotalTimeMs());
        assertNull(metrics.getTtfbMs());

        // Check the timing metrics
        assertNotNull(metrics.getRequestStart());
        assertTrue(metrics.getRequestStart().after(startTime)
                || metrics.getRequestStart().equals(startTime));
        MetricsTestUtil.checkNoConnectTiming(metrics);
        assertNull(metrics.getSendingStart());
        assertNull(metrics.getSendingEnd());
        assertNull(metrics.getResponseStart());
        assertNotNull(metrics.getRequestEnd());
        assertTrue(
                metrics.getRequestEnd().before(endTime) || metrics.getRequestEnd().equals(endTime));
        // Entire request should take more than 0 ms
        assertTrue(metrics.getRequestEnd().getTime() - metrics.getRequestStart().getTime() > 0);
        assertTrue(metrics.getSentBytesCount() == 0);
        assertTrue(metrics.getReceivedBytesCount() == 0);
        mTestFramework.mCronetEngine.shutdown();
    }

    @SmallTest
    @OnlyRunNativeCronet
    @Feature({"Cronet"})
    @SuppressWarnings("deprecation")
    public void testRequestFinishedListenerRemoved() throws Exception {
        mTestFramework = startCronetTestFramework();
        TestExecutor testExecutor = new TestExecutor();
        TestRequestFinishedListener requestFinishedListener =
                new TestRequestFinishedListener(testExecutor);
        mTestFramework.mCronetEngine.addRequestFinishedListener(requestFinishedListener);
        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder urlRequestBuilder = new UrlRequest.Builder(
                mUrl, callback, callback.getExecutor(), mTestFramework.mCronetEngine);
        UrlRequest request = urlRequestBuilder.build();
        mTestFramework.mCronetEngine.removeRequestFinishedListener(requestFinishedListener);
        request.start();
        callback.blockForDone();
        testExecutor.runAllTasks();

        assertNull("RequestFinishedInfo.Listener must not be called",
                requestFinishedListener.getRequestInfo());
        mTestFramework.mCronetEngine.shutdown();
    }

    @SmallTest
    @OnlyRunNativeCronet
    @Feature({"Cronet"})
    public void testRequestFinishedListenerCanceledRequest() throws Exception {
        mTestFramework = startCronetTestFramework();
        TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
        mTestFramework.mCronetEngine.addRequestFinishedListener(requestFinishedListener);
        TestUrlRequestCallback callback = new TestUrlRequestCallback() {
            @Override
            public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
                super.onResponseStarted(request, info);
                request.cancel();
            }
        };
        UrlRequest.Builder urlRequestBuilder = new UrlRequest.Builder(
                mUrl, callback, callback.getExecutor(), mTestFramework.mCronetEngine);
        Date startTime = new Date();
        urlRequestBuilder.addRequestAnnotation("request annotation")
                .addRequestAnnotation(this)
                .build()
                .start();
        callback.blockForDone();
        requestFinishedListener.blockUntilDone();
        Date endTime = new Date();

        RequestFinishedInfo requestInfo = requestFinishedListener.getRequestInfo();
        assertNotNull("RequestFinishedInfo.Listener must be called", requestInfo);
        assertEquals(mUrl, requestInfo.getUrl());
        assertNotNull(requestInfo.getResponseInfo());
        assertEquals(newHashSet("request annotation", this), // Use sets for unordered comparison.
                new HashSet<Object>(requestInfo.getAnnotations()));
        RequestFinishedInfo.Metrics metrics = requestInfo.getMetrics();
        assertNotNull("RequestFinishedInfo.getMetrics() must not be null", metrics);
        // Check old (deprecated) timing metrics
        assertTrue(metrics.getTotalTimeMs() > 0);
        assertTrue(metrics.getTotalTimeMs() >= metrics.getTtfbMs());
        // Check new timing metrics
        MetricsTestUtil.checkTimingMetrics(metrics, startTime, endTime);
        MetricsTestUtil.checkHasConnectTiming(metrics, startTime, endTime, false);
        assertNull(metrics.getPushStart());
        assertNull(metrics.getPushEnd());
        // Check data use metrics
        assertTrue(metrics.getSentBytesCount() > 0);
        assertTrue(metrics.getReceivedBytesCount() > 0);
        mTestFramework.mCronetEngine.shutdown();
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMetricsGetters() throws Exception {
        long requestStart = 1;
        long dnsStart = 2;
        long dnsEnd = -1;
        long connectStart = 4;
        long connectEnd = 5;
        long sslStart = 6;
        long sslEnd = 7;
        long sendingStart = 8;
        long sendingEnd = 9;
        long pushStart = 10;
        long pushEnd = 11;
        long responseStart = 12;
        long requestEnd = 13;
        boolean socketReused = true;
        long sentBytesCount = 14;
        long receivedBytesCount = 15;
        // Make sure nothing gets reordered inside the Metrics class
        RequestFinishedInfo.Metrics metrics = new CronetMetrics(requestStart, dnsStart, dnsEnd,
                connectStart, connectEnd, sslStart, sslEnd, sendingStart, sendingEnd, pushStart,
                pushEnd, responseStart, requestEnd, socketReused, sentBytesCount,
                receivedBytesCount);
        assertEquals(new Date(requestStart), metrics.getRequestStart());
        // -1 timestamp should translate to null
        assertNull(metrics.getDnsEnd());
        assertEquals(new Date(dnsStart), metrics.getDnsStart());
        assertEquals(new Date(connectStart), metrics.getConnectStart());
        assertEquals(new Date(connectEnd), metrics.getConnectEnd());
        assertEquals(new Date(sslStart), metrics.getSslStart());
        assertEquals(new Date(sslEnd), metrics.getSslEnd());
        assertEquals(new Date(pushStart), metrics.getPushStart());
        assertEquals(new Date(pushEnd), metrics.getPushEnd());
        assertEquals(new Date(responseStart), metrics.getResponseStart());
        assertEquals(new Date(requestEnd), metrics.getRequestEnd());
        assertEquals(socketReused, metrics.getSocketReused());
        assertEquals(sentBytesCount, (long) metrics.getSentBytesCount());
        assertEquals(receivedBytesCount, (long) metrics.getReceivedBytesCount());
    }
}
