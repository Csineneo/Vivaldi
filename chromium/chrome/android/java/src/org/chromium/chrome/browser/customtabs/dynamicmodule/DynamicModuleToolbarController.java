// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs.dynamicmodule;

import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.browser.customtabs.CustomTabBrowserControlsVisibilityDelegate;
import org.chromium.chrome.browser.customtabs.CustomTabIntentDataProvider;
import org.chromium.chrome.browser.dependency_injection.ActivityScope;
import org.chromium.chrome.browser.fullscreen.ChromeFullscreenManager;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.init.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.InflationObserver;
import org.chromium.chrome.browser.lifecycle.NativeInitObserver;

import javax.inject.Inject;

import dagger.Lazy;

/**
 * Controls the visibility of the toolbar on module managed URLs.
 */
@ActivityScope
public class DynamicModuleToolbarController implements InflationObserver, NativeInitObserver {
    private final Lazy<ChromeFullscreenManager> mFullscreenManager;
    private final CustomTabBrowserControlsVisibilityDelegate mControlsVisibilityDelegate;
    private final CustomTabIntentDataProvider mIntentDataProvider;

    private int mControlsHidingToken = FullscreenManager.INVALID_TOKEN;
    private boolean mHasReleasedToken;

    @Inject
    public DynamicModuleToolbarController(Lazy<ChromeFullscreenManager> fullscreenManager,
            CustomTabBrowserControlsVisibilityDelegate controlsVisibilityDelegate,
            CustomTabIntentDataProvider intentDataProvider,
            ActivityLifecycleDispatcher activityLifecycleDispatcher) {
        this.mFullscreenManager = fullscreenManager;
        this.mControlsVisibilityDelegate = controlsVisibilityDelegate;
        this.mIntentDataProvider = intentDataProvider;

        activityLifecycleDispatcher.register(this);
    }

    @Override
    public void onPreInflationStartup() {}

    @Override
    public void onPostInflationStartup() {
        mControlsVisibilityDelegate.setModuleLoadingMode(true);
        mControlsHidingToken =
                mFullscreenManager.get().hideAndroidControlsAndClearOldToken(mControlsHidingToken);
        mHasReleasedToken = false;
    }

    @Override
    public void onFinishNativeInitialization() {
        if (!mIntentDataProvider.isDynamicModuleEnabled()) {
            releaseAndroidControlsHidingToken();
        }
    }

    /* package */ void releaseAndroidControlsHidingToken() {
        mControlsVisibilityDelegate.setModuleLoadingMode(false);
        mFullscreenManager.get().releaseAndroidControlsHidingToken(mControlsHidingToken);
        mHasReleasedToken = true;
    }

    @VisibleForTesting
    boolean hasReleasedToken() {
        return mHasReleasedToken;
    }

    @VisibleForTesting
    boolean hasAcquiredToken() {
        return mControlsHidingToken != FullscreenManager.INVALID_TOKEN;
    }
}
