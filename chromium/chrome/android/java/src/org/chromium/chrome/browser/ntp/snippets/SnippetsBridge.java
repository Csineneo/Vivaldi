// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.snippets;

import android.graphics.Bitmap;

import org.chromium.base.Callback;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.chrome.browser.profiles.Profile;

import java.util.ArrayList;
import java.util.List;

/**
 * Provides access to the snippets to display on the NTP using the C++ NTP Snippets Service
 */
public class SnippetsBridge {
    private static final String TAG = "SnippetsBridge";

    private long mNativeSnippetsBridge;
    private SnippetsObserver mObserver;

    /**
     * A callback that is called after a snippet image was fetched from the snippets service.
     */
    public interface FetchSnippetImageCallback {
        @CalledByNative("FetchSnippetImageCallback")
        void onSnippetImageAvailable(Bitmap image);
    }

    /**
     * An observer for events in the snippets service.
     */
    public interface SnippetsObserver {
        void onSnippetsReceived(List<SnippetArticle> snippets);

        /** Called when the service has been disabled. */
        void onSnippetsDisabled();
    }

    /**
     * Creates a SnippetsBridge for getting snippet data for the current user.
     *
     * @param profile Profile of the user that we will retrieve snippets for.
     */
    public SnippetsBridge(Profile profile) {
        mNativeSnippetsBridge = nativeInit(profile);
    }

    /**
     * Destroys the native service and unregisters observers. This object can't be reused to
     * communicate with any native service and should be discarded.
     */
    public void destroy() {
        assert mNativeSnippetsBridge != 0;
        nativeDestroy(mNativeSnippetsBridge);
        mNativeSnippetsBridge = 0;
        mObserver = null;
    }

    /**
     * Fetches new snippets.
     */
    public static void fetchSnippets() {
        nativeFetchSnippets();
    }

    /**
     * Reschedules the fetching of snippets. Used to support different fetching intervals for
     * different times of day.
     */
    public static void rescheduleFetching() {
        nativeRescheduleFetching();
    }

    /**
     * Tells the native service to discard a snippet. It will be removed from the native side
     * storage and will also be discarded from subsequent fetch results.
     *
     * @param snippet Snippet to discard.
     */
    public void discardSnippet(SnippetArticle snippet) {
        assert mNativeSnippetsBridge != 0;
        nativeDiscardSnippet(mNativeSnippetsBridge, snippet.mId);
    }

    /**
     * Fetches the thumbnail image for a snippet.
     */
    public void fetchSnippetImage(SnippetArticle snippet, FetchSnippetImageCallback callback) {
        nativeFetchImage(mNativeSnippetsBridge, snippet.mId, callback);
    }

    /**
     * Checks whether a snippet has been visited by querying the history for the snippet's URL.
     */
    public void getSnippedVisited(SnippetArticle snippet, Callback<Boolean> callback) {
        assert mNativeSnippetsBridge != 0;
        nativeSnippetVisited(mNativeSnippetsBridge, callback, snippet.mUrl);
    }

    /**
     * {@link Callback#onResult} is not annotated with CalledByNative, so we must use this wrapper.
     */
    @CalledByNative
    private static void runCallback(Callback<Boolean> callback, boolean result) {
        callback.onResult(result);
    }

    /**
     * Sets the recipient for the fetched snippets.
     *
     * An observer needs to be set before the native code attempts to transmit snippets them to
     * java. Upon registration, the observer will be notified of already fetched snippets.
     *
     * @param observer object to notify when snippets are received, or {@code null} if we want to
     *                 stop observing.
     */
    public void setObserver(SnippetsObserver observer) {
        assert mObserver == null || mObserver == observer;

        mObserver = observer;
        nativeSetObserver(mNativeSnippetsBridge, observer == null ? null : this);
    }

    @CalledByNative
    private void onSnippetsAvailable(String[] ids, String[] titles, String[] urls, String[] ampUrls,
            String[] thumbnailUrls, String[] previewText, long[] timestamps, String[] publishers) {
        // Don't notify observer if we've already been destroyed.
        if (mNativeSnippetsBridge == 0) return;
        assert mObserver != null;

        List<SnippetArticle> newSnippets = new ArrayList<>(ids.length);
        for (int i = 0; i < ids.length; i++) {
            newSnippets.add(new SnippetArticle(ids[i], titles[i], publishers[i], previewText[i],
                    urls[i], ampUrls[i], thumbnailUrls[i], timestamps[i], i));
        }

        mObserver.onSnippetsReceived(newSnippets);
    }

    @CalledByNative
    private void onSnippetsDisabled() {
        if (mObserver != null) mObserver.onSnippetsDisabled();
    }

    private native long nativeInit(Profile profile);
    private native void nativeDestroy(long nativeNTPSnippetsBridge);
    private static native void nativeFetchSnippets();
    private static native void nativeRescheduleFetching();
    private native void nativeDiscardSnippet(long nativeNTPSnippetsBridge, String snippetId);
    private native void nativeSetObserver(long nativeNTPSnippetsBridge, SnippetsBridge bridge);
    private static native void nativeSnippetVisited(long nativeNTPSnippetsBridge,
            Callback<Boolean> callback, String url);
    private native void nativeFetchImage(
            long nativeNTPSnippetsBridge, String snippetId, FetchSnippetImageCallback callback);
}
