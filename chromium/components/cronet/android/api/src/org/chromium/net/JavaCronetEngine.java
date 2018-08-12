// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import static android.os.Process.THREAD_PRIORITY_BACKGROUND;
import static android.os.Process.THREAD_PRIORITY_MORE_FAVORABLE;

import java.io.IOException;
import java.net.Proxy;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.net.URLStreamHandlerFactory;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

/**
 * {@link java.net.HttpURLConnection} backed CronetEngine.
 *
 * <p>Does not support netlogs, transferred data measurement, bidistream, cache, or priority.
 */
final class JavaCronetEngine extends CronetEngine {
    private final String mUserAgent;

    private final ExecutorService mExecutorService =
            Executors.newCachedThreadPool(new ThreadFactory() {
                @Override
                public Thread newThread(final Runnable r) {
                    return Executors.defaultThreadFactory().newThread(new Runnable() {
                        @Override
                        public void run() {
                            Thread.currentThread().setName("JavaCronetEngine");
                            // On android, all background threads (and all threads that are part
                            // of background processes) are put in a cgroup that is allowed to
                            // consume up to 5% of CPU - these worker threads spend the vast
                            // majority of their time waiting on I/O, so making them contend with
                            // background applications for a slice of CPU doesn't make much sense.
                            // We want to hurry up and get idle.
                            android.os.Process.setThreadPriority(
                                    THREAD_PRIORITY_BACKGROUND + THREAD_PRIORITY_MORE_FAVORABLE);
                            r.run();
                        }
                    });
                }
            });

    JavaCronetEngine(String userAgent) {
        this.mUserAgent = userAgent;
    }

    @Override
    public UrlRequest createRequest(String url, UrlRequest.Callback callback, Executor executor,
            int priority, Collection<Object> connectionAnnotations) {
        return new JavaUrlRequest(callback, mExecutorService, executor, url, mUserAgent);
    }

    @Override
    BidirectionalStream createBidirectionalStream(String url, BidirectionalStream.Callback callback,
            Executor executor, String httpMethod, List<Map.Entry<String, String>> requestHeaders,
            @BidirectionalStream.Builder.StreamPriority int priority) {
        throw new UnsupportedOperationException(
                "Can't create a bidi stream - httpurlconnection doesn't have those APIs");
    }

    @Override
    boolean isEnabled() {
        return true;
    }

    @Override
    public String getVersionString() {
        return "CronetHttpURLConnection/" + Version.getVersion();
    }

    @Override
    public void shutdown() {
        mExecutorService.shutdown();
    }

    @Override
    public void startNetLogToFile(String fileName, boolean logAll) {}

    @Override
    public void stopNetLog() {}

    @Override
    public byte[] getGlobalMetricsDeltas() {
        return new byte[0];
    }

    @Override
    public void enableNetworkQualityEstimator(Executor executor) {}

    @Override
    void enableNetworkQualityEstimatorForTesting(
            boolean useLocalHostRequests, boolean useSmallerResponses, Executor executor) {}

    @Override
    public void addRttListener(NetworkQualityRttListener listener) {}

    @Override
    public void removeRttListener(NetworkQualityRttListener listener) {}

    @Override
    public void addThroughputListener(NetworkQualityThroughputListener listener) {}

    @Override
    public void removeThroughputListener(NetworkQualityThroughputListener listener) {}

    @Override
    public void addRequestFinishedListener(RequestFinishedListener listener) {}

    @Override
    public void removeRequestFinishedListener(RequestFinishedListener listener) {}

    @Override
    public URLConnection openConnection(URL url) throws IOException {
        return url.openConnection();
    }

    @Override
    public URLConnection openConnection(URL url, Proxy proxy) throws IOException {
        return url.openConnection(proxy);
    }

    @Override
    public URLStreamHandlerFactory createURLStreamHandlerFactory() {
        // Returning null causes this factory to pass though, which ends up using the platform's
        // implementation.
        return new URLStreamHandlerFactory() {
            @Override
            public URLStreamHandler createURLStreamHandler(String protocol) {
                return null;
            }
        };
    }
}
