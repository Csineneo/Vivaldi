// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.snippets;

import android.content.Context;

import com.google.android.gms.gcm.GcmNetworkManager;
import com.google.android.gms.gcm.PeriodicTask;
import com.google.android.gms.gcm.Task;

import org.chromium.base.Log;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.SuppressFBWarnings;
import org.chromium.chrome.browser.ChromeBackgroundService;
import org.chromium.chrome.browser.externalauth.ExternalAuthUtils;
import org.chromium.chrome.browser.externalauth.UserRecoverableErrorHandler;

/**
 * The {@link SnippetsLauncher} singleton is created and owned by the C++ browser.
 *
 * Thread model: This class is to be run on the UI thread only.
 */
public class SnippetsLauncher {
    private static final String TAG = "SnippetsLauncher";

    public static final String TASK_TAG_WIFI_CHARGING = "FetchSnippetsWifiCharging";
    public static final String TASK_TAG_WIFI = "FetchSnippetsWifi";
    public static final String TASK_TAG_FALLBACK = "FetchSnippetsFallback";

    // The instance of SnippetsLauncher currently owned by a C++ SnippetsLauncherAndroid, if any.
    // If it is non-null then the browser is running.
    private static SnippetsLauncher sInstance;

    private GcmNetworkManager mScheduler;

    private boolean mGCMEnabled = true;

    /**
     * Create a SnippetsLauncher object, which is owned by C++.
     * @param context The app context.
     */
    @VisibleForTesting
    @CalledByNative
    public static SnippetsLauncher create(Context context) {
        if (sInstance != null) {
            throw new IllegalStateException("Already instantiated");
        }

        sInstance = new SnippetsLauncher(context);
        return sInstance;
    }

    /**
     * Called when the C++ counterpart is deleted.
     */
    @VisibleForTesting
    @SuppressFBWarnings("ST_WRITE_TO_STATIC_FROM_INSTANCE_METHOD")
    @CalledByNative
    public void destroy() {
        assert sInstance == this;
        sInstance = null;
    }

    /**
     * Returns true if the native browser has started and created an instance of {@link
     * SnippetsLauncher}.
     */
    public static boolean hasInstance() {
        return sInstance != null;
    }

    protected SnippetsLauncher(Context context) {
        checkGCM(context);
        mScheduler = GcmNetworkManager.getInstance(context);
    }

    private boolean canUseGooglePlayServices(Context context) {
        return ExternalAuthUtils.getInstance().canUseGooglePlayServices(
                context, new UserRecoverableErrorHandler.Silent());
    }

    private void checkGCM(Context context) {
        // Check to see if Play Services is up to date, and disable GCM if not.
        if (!canUseGooglePlayServices(context)) {
            mGCMEnabled = false;
            Log.i(TAG, "Disabling SnippetsLauncher because Play Services is not up to date.");
        }
    }

    private static PeriodicTask buildTask(
            String tag, long periodSeconds, int requiredNetwork, boolean requiresCharging) {
        return new PeriodicTask.Builder()
                .setService(ChromeBackgroundService.class)
                .setTag(tag)
                .setPeriod(periodSeconds)
                .setRequiredNetwork(requiredNetwork)
                .setRequiresCharging(requiresCharging)
                .setPersisted(true)
                .setUpdateCurrent(true)
                .build();
    }

    @CalledByNative
    private boolean schedule(
            long periodWifiChargingSeconds, long periodWifiSeconds, long periodFallbackSeconds) {
        if (!mGCMEnabled) return false;
        Log.d(TAG, "Scheduling: " + periodWifiChargingSeconds + " " + periodWifiSeconds + " "
                        + periodFallbackSeconds);
        // Google Play Services may not be up to date, if the application was not installed through
        // the Play Store. In this case, scheduling the task will fail silently.
        try {
            mScheduler.schedule(buildTask(TASK_TAG_WIFI_CHARGING, periodWifiChargingSeconds,
                    Task.NETWORK_STATE_UNMETERED, true));
            mScheduler.schedule(buildTask(
                    TASK_TAG_WIFI, periodWifiSeconds, Task.NETWORK_STATE_UNMETERED, false));
            mScheduler.schedule(buildTask(
                    TASK_TAG_FALLBACK, periodFallbackSeconds, Task.NETWORK_STATE_CONNECTED, false));
        } catch (IllegalArgumentException e) {
            // Disable GCM for the remainder of this session.
            mGCMEnabled = false;
            // Return false so that the failure will be logged.
            return false;
        }
        return true;
    }

    @CalledByNative
    private boolean unschedule() {
        if (!mGCMEnabled) return false;
        Log.i(TAG, "Unscheduling");
        try {
            mScheduler.cancelTask(TASK_TAG_WIFI_CHARGING, ChromeBackgroundService.class);
            mScheduler.cancelTask(TASK_TAG_WIFI, ChromeBackgroundService.class);
            mScheduler.cancelTask(TASK_TAG_FALLBACK, ChromeBackgroundService.class);
        } catch (IllegalArgumentException e) {
            // This occurs when SnippetsLauncherService is not found in the application
            // manifest. Disable GCM for the remainder of this session.
            mGCMEnabled = false;
            // Return false so that the failure will be logged.
            return false;
        }
        return true;
    }
}

