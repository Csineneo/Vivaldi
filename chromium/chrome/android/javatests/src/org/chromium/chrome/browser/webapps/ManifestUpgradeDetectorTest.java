// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.webapps;

import android.content.Context;
import android.os.Bundle;
import android.os.Environment;
import android.test.suitebuilder.annotation.MediumTest;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Feature;
import org.chromium.blink_public.platform.WebDisplayMode;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.test.ChromeTabbedActivityTestBase;
import org.chromium.chrome.test.util.browser.WebappTestPage;
import org.chromium.content.browser.test.util.CallbackHelper;
import org.chromium.content_public.common.ScreenOrientationValues;
import org.chromium.net.test.EmbeddedTestServer;
import org.chromium.webapk.lib.common.WebApkMetaDataKeys;

/**
 * Tests ManifestUpgradeDetector. This class contains tests which cannot be done as JUnit tests.
 */
public class ManifestUpgradeDetectorTest extends ChromeTabbedActivityTestBase {

    private static final String WEBAPK_MANIFEST_URL =
            "/chrome/test/data/banners/manifest_one_icon.json";

    // Data contained in {@link WEBAPK_MANIFEST_URL}.
    private static final String WEBAPK_START_URL =
            "/chrome/test/data/banners/manifest_test_page.html";
    private static final String WEBAPK_SCOPE_URL = "/chrome/test/data/banners/";
    private static final String WEBAPK_NAME = "Manifest test app";
    private static final String WEBAPK_SHORT_NAME = "Manifest test app";
    private static final String WEBAPK_ICON_URL = "/chrome/test/data/banners/image-512px.png";
    private static final long WEBAPK_ICON_MURMUR2_HASH = 6537363487080720023L;
    private static final int WEBAPK_DISPLAY_MODE = WebDisplayMode.Standalone;
    private static final int WEBAPK_ORIENTATION = ScreenOrientationValues.LANDSCAPE;
    private static final long WEBAPK_THEME_COLOR = 2147483648L;
    private static final long WEBAPK_BACKGROUND_COLOR = 2147483648L;

    private EmbeddedTestServer mTestServer;
    private Tab mTab;

    // CallbackHelper which blocks until the {@link ManifestUpgradeDetector.Callback} callback is
    // called.
    private static class CallbackWaiter
            extends CallbackHelper implements ManifestUpgradeDetector.Callback {
        private String mName;
        private boolean mNeedsUpgrade;

        @Override
        public void onUpgradeNeededCheckFinished(
                boolean needsUpgrade, ManifestUpgradeDetector.FetchedManifestData data) {
            mName = data.name;
            mNeedsUpgrade = needsUpgrade;
            notifyCalled();
        }

        public String name() {
            return mName;
        }

        public boolean needsUpgrade() {
            return mNeedsUpgrade;
        }
    }

    private static class CreationData {
        public CreationData(EmbeddedTestServer server) {
            startUrl = server.getURL(WEBAPK_START_URL);
            scopeUrl = server.getURL(WEBAPK_SCOPE_URL);
            name = WEBAPK_NAME;
            shortName = WEBAPK_SHORT_NAME;
            iconUrl = server.getURL(WEBAPK_ICON_URL);
            iconMurmur2Hash = WEBAPK_ICON_MURMUR2_HASH;
            displayMode = WEBAPK_DISPLAY_MODE;
            orientation = WEBAPK_ORIENTATION;
            themeColor = WEBAPK_THEME_COLOR;
            backgroundColor = WEBAPK_BACKGROUND_COLOR;
        }

        public String startUrl;
        public String scopeUrl;
        public String name;
        public String shortName;
        public String iconUrl;
        public long iconMurmur2Hash;
        public int displayMode;
        public int orientation;
        public long themeColor;
        public long backgroundColor;
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        Context context = getInstrumentation().getTargetContext();
        mTestServer = EmbeddedTestServer.createAndStartFileServer(
                context, Environment.getExternalStorageDirectory());
        mTab = getActivity().getActivityTab();
    }

    @Override
    protected void tearDown() throws Exception {
        mTestServer.stopAndDestroyServer();
        super.tearDown();
    }

    @Override
    public void startMainActivity() throws InterruptedException {
        startMainActivityOnBlankPage();
    }

    /**
     * Starts a ManifestUpgradeDetector. Calls {@link callback} once the detector has fetched the
     * Web Manifest and determined whether the WebAPK needs to be upgraded.
     */
    private void startManifestUpgradeDetector(
            CreationData creationData, final ManifestUpgradeDetector.Callback callback) {
        Bundle metadata = new Bundle();
        metadata.putString(
                WebApkMetaDataKeys.WEB_MANIFEST_URL, mTestServer.getURL(WEBAPK_MANIFEST_URL));
        metadata.putString(WebApkMetaDataKeys.START_URL, creationData.startUrl);
        metadata.putString(WebApkMetaDataKeys.ICON_URL, creationData.iconUrl);
        metadata.putString(
                WebApkMetaDataKeys.ICON_MURMUR2_HASH, creationData.iconMurmur2Hash + "L");
        WebappInfo webappInfo = WebappInfo.create("", creationData.startUrl, creationData.scopeUrl,
                null, creationData.name, creationData.shortName, creationData.displayMode,
                creationData.orientation, 0, creationData.themeColor, creationData.backgroundColor,
                false, null);

        final ManifestUpgradeDetector detector =
                new ManifestUpgradeDetector(mTab, webappInfo, metadata, callback);

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                detector.start();
            }
        });
    }

    /**
     * Test that the canonicalized URLs are used in determining whether the fetched Web Manifest
     * data differs from the metadata in the WebAPK's Android Manifest. This is important because
     * the URLs in the Web Manifest have been modified by the WebAPK server prior to being stored in
     * the WebAPK Android Manifest. Chrome and the WebAPK server parse URLs differently.
     */
    @MediumTest
    @Feature({"WebApk"})
    public void testCanonicalUrlsIdenticalShouldNotUpgrade() throws Exception {
        CallbackWaiter waiter = new CallbackWaiter();

        // URL canonicalization should replace "%74" with 't'.
        CreationData creationData = new CreationData(mTestServer);
        creationData.startUrl = mTestServer.getURL(
                "/chrome/test/data/banners/manifest_%74est_page.html");
        startManifestUpgradeDetector(creationData, waiter);

        WebappTestPage.navigateToPageWithServiceWorkerAndManifest(
                mTestServer, mTab, WEBAPK_MANIFEST_URL);
        waiter.waitForCallback(0);

        assertEquals(WEBAPK_NAME, waiter.name());
        assertFalse(waiter.needsUpgrade());
    }

    /**
     * Test that an upgraded WebAPK is requested if the canonicalized "start URLs" are different.
     */
    @MediumTest
    @Feature({"WebApk"})
    public void testCanonicalUrlsDifferentShouldUpgrade() throws Exception {
        CallbackWaiter waiter = new CallbackWaiter();

        // URL canonicalization should replace "%62" with 'b'.
        CreationData creationData = new CreationData(mTestServer);
        creationData.startUrl = mTestServer.getURL(
                "/chrome/test/data/banners/manifest_%62est_page.html");
        startManifestUpgradeDetector(creationData, waiter);

        WebappTestPage.navigateToPageWithServiceWorkerAndManifest(
                mTestServer, mTab, WEBAPK_MANIFEST_URL);
        waiter.waitForCallback(0);

        assertEquals(WEBAPK_NAME, waiter.name());
        assertTrue(waiter.needsUpgrade());
    }
}
