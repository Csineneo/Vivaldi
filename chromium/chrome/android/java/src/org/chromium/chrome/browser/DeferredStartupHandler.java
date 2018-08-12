// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.content.Context;
import android.os.AsyncTask;
import android.os.SystemClock;

import org.chromium.base.FieldTrialList;
import org.chromium.base.PowerMonitor;
import org.chromium.base.SysUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.bookmarkswidget.BookmarkWidgetProvider;
import org.chromium.chrome.browser.crash.CrashFileManager;
import org.chromium.chrome.browser.crash.MinidumpUploadService;
import org.chromium.chrome.browser.media.MediaCaptureNotificationService;
import org.chromium.chrome.browser.metrics.LaunchMetrics;
import org.chromium.chrome.browser.metrics.UmaUtils;
import org.chromium.chrome.browser.ntp.NewTabPage;
import org.chromium.chrome.browser.partnerbookmarks.PartnerBookmarksShim;
import org.chromium.chrome.browser.partnercustomizations.HomepageManager;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.physicalweb.PhysicalWeb;
import org.chromium.chrome.browser.precache.PrecacheLauncher;
import org.chromium.chrome.browser.preferences.ChromePreferenceManager;
import org.chromium.chrome.browser.preferences.privacy.PrivacyPreferencesManager;
import org.chromium.chrome.browser.share.ShareHelper;
import org.chromium.content.browser.ChildProcessLauncher;

import java.util.concurrent.TimeUnit;

/**
 * Handler for application level tasks to be completed on deferred startup.
 */
public class DeferredStartupHandler {
    private static DeferredStartupHandler sDeferredStartupHandler;
    private boolean mDeferredStartupComplete;

    /**
     * This class is an application specific object that handles the deferred startup.
     * @return The singleton instance of {@link DeferredStartupHandler}.
     */
    public static DeferredStartupHandler getInstance() {
        if (sDeferredStartupHandler == null) {
            sDeferredStartupHandler = new DeferredStartupHandler();
        }
        return sDeferredStartupHandler;
    }

    private DeferredStartupHandler() { }

    /**
     * Handle application level deferred startup tasks that can be lazily done after all
     * the necessary initialization has been completed. Any calls requiring network access should
     * probably go here.
     * @param application The application object to use for context.
     * @param crashDumpUploadingCommandLineDisabled Whether crash dump uploading should be disabled.
     */
    public void onDeferredStartup(final ChromeApplication application,
            final boolean crashDumpUploadingCommandLineDisabled) {
        if (mDeferredStartupComplete) return;
        ThreadUtils.assertOnUiThread();

        RecordHistogram.recordLongTimesHistogram("UMA.Debug.EnableCrashUpload.DeferredStartUptime",
                SystemClock.uptimeMillis() - UmaUtils.getMainEntryPointTime(),
                TimeUnit.MILLISECONDS);

        // Punt all tasks that may block the UI thread off onto a background thread.
        new AsyncTask<Void, Void, Void>() {
            @Override
            protected Void doInBackground(Void... params) {
                try {
                    TraceEvent.begin("ChromeBrowserInitializer.onDeferredStartup.doInBackground");
                    if (!crashDumpUploadingCommandLineDisabled) {
                        RecordHistogram.recordLongTimesHistogram(
                                "UMA.Debug.EnableCrashUpload.Uptime2",
                                SystemClock.uptimeMillis() - UmaUtils.getMainEntryPointTime(),
                                TimeUnit.MILLISECONDS);
                        PrivacyPreferencesManager.getInstance(application)
                                .enablePotentialCrashUploading();
                        MinidumpUploadService.tryUploadAllCrashDumps(application);
                    }
                    CrashFileManager crashFileManager =
                            new CrashFileManager(application.getCacheDir());
                    crashFileManager.cleanOutAllNonFreshMinidumpFiles();

                    MinidumpUploadService.storeBreakpadUploadStatsInUma(
                            ChromePreferenceManager.getInstance(application));

                    // Force a widget refresh in order to wake up any possible zombie widgets.
                    // This is needed to ensure the right behavior when the process is suddenly
                    // killed.
                    BookmarkWidgetProvider.refreshAllWidgets(application);

                    // Initialize whether or not precaching is enabled.
                    PrecacheLauncher.updatePrecachingEnabled(application);

                    return null;
                } finally {
                    TraceEvent.end("ChromeBrowserInitializer.onDeferredStartup.doInBackground");
                }
            }
        }.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);

        AfterStartupTaskUtils.setStartupComplete();

        PartnerBrowserCustomizations.setOnInitializeAsyncFinished(new Runnable() {
            @Override
            public void run() {
                String homepageUrl = HomepageManager.getHomepageUri(application);
                LaunchMetrics.recordHomePageLaunchMetrics(
                        HomepageManager.isHomepageEnabled(application),
                        NewTabPage.isNTPUrl(homepageUrl), homepageUrl);
            }
        });

        // TODO(aruslan): http://b/6397072 This will be moved elsewhere
        PartnerBookmarksShim.kickOffReading(application);

        PowerMonitor.create(application);

        // Starts syncing with GSA.
        application.createGsaHelper().startSync();

        application.initializeSharedClasses();

        ShareHelper.clearSharedImages(application);

        // Clear any media notifications that existed when Chrome was last killed.
        MediaCaptureNotificationService.clearMediaNotifications(application);

        startModerateBindingManagementIfNeeded(application);

        String customTabsTrialGroupName = FieldTrialList.findFullName("CustomTabs");
        if (customTabsTrialGroupName.equals("Disabled")) {
            ChromePreferenceManager.getInstance(application).setCustomTabsEnabled(false);
        } else if (customTabsTrialGroupName.equals("Enabled")
                || customTabsTrialGroupName.equals("DisablePrerender")) {
            ChromePreferenceManager.getInstance(application).setCustomTabsEnabled(true);
        }

        // Start or stop Physical Web
        PhysicalWeb.onChromeStart(application);

        mDeferredStartupComplete = true;
    }

    private static void startModerateBindingManagementIfNeeded(Context context) {
        // Moderate binding doesn't apply to low end devices.
        if (SysUtils.isLowEndDevice()) return;

        boolean moderateBindingTillBackgrounded =
                FieldTrialList.findFullName("ModerateBindingOnBackgroundTabCreation")
                        .equals("Enabled");
        ChildProcessLauncher.startModerateBindingManagement(
                context, moderateBindingTillBackgrounded);
    }

    /**
    * @return Whether deferred startup has been completed.
    */
    @VisibleForTesting
    public boolean isDeferredStartupComplete() {
        return mDeferredStartupComplete;
    }
}
