// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp;

import android.annotation.TargetApi;
import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.SystemClock;
import android.view.ContextMenu;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem.OnMenuItemClickListener;
import android.view.View;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Callback;
import org.chromium.base.CommandLine;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.NativePage;
import org.chromium.chrome.browser.UrlConstants;
import org.chromium.chrome.browser.bookmarks.BookmarkUtils;
import org.chromium.chrome.browser.compositor.layouts.content.InvalidationAwareThumbnailProvider;
import org.chromium.chrome.browser.favicon.FaviconHelper;
import org.chromium.chrome.browser.favicon.FaviconHelper.FaviconImageCallback;
import org.chromium.chrome.browser.favicon.FaviconHelper.IconAvailabilityCallback;
import org.chromium.chrome.browser.favicon.LargeIconBridge;
import org.chromium.chrome.browser.favicon.LargeIconBridge.LargeIconCallback;
import org.chromium.chrome.browser.metrics.StartupMetrics;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.ntp.LogoBridge.Logo;
import org.chromium.chrome.browser.ntp.LogoBridge.LogoObserver;
import org.chromium.chrome.browser.ntp.NewTabPageView.NewTabPageManager;
import org.chromium.chrome.browser.ntp.interests.InterestsPage;
import org.chromium.chrome.browser.ntp.interests.InterestsPage.InterestsClickListener;
import org.chromium.chrome.browser.ntp.snippets.SnippetsBridge;
import org.chromium.chrome.browser.ntp.snippets.SnippetsConfig;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.chrome.browser.profiles.MostVisitedSites;
import org.chromium.chrome.browser.profiles.MostVisitedSites.MostVisitedURLsObserver;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.search_engines.TemplateUrlService;
import org.chromium.chrome.browser.search_engines.TemplateUrlService.TemplateUrlServiceObserver;
import org.chromium.chrome.browser.snackbar.Snackbar;
import org.chromium.chrome.browser.snackbar.SnackbarManager.SnackbarController;
import org.chromium.chrome.browser.sync.SyncSessionsMetrics;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModel.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tabmodel.document.TabDelegate;
import org.chromium.chrome.browser.util.UrlUtilities;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.net.NetworkChangeNotifier;
import org.chromium.sync.signin.ChromeSigninController;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.PageTransition;

import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;

import jp.tomorrowkey.android.gifplayer.BaseGifImage;

/**
 * Provides functionality when the user interacts with the NTP.
 */
public class NewTabPage
        implements NativePage, InvalidationAwareThumbnailProvider, TemplateUrlServiceObserver {

    // MostVisitedItem Context menu item IDs.
    static final int ID_OPEN_IN_NEW_WINDOW = 0;
    static final int ID_OPEN_IN_NEW_TAB = 1;
    static final int ID_OPEN_IN_INCOGNITO_TAB = 2;
    static final int ID_REMOVE = 3;

    // The name of the trial for obtaining variation parameters for the ntp snippets feature. Also
    // defined in ntp_snippets_constants.cc.
    public static final String FIELD_TRIAL_NAME = "NTPSnippets";

    // UMA enum constants. CTA means the "click-to-action" icon.
    private static final String LOGO_SHOWN_UMA_NAME = "NewTabPage.LogoShown";
    private static final int STATIC_LOGO_SHOWN = 0;
    private static final int CTA_IMAGE_SHOWN = 1;

    private static final String LOGO_CLICK_UMA_NAME = "NewTabPage.LogoClick";
    private static final int STATIC_LOGO_CLICKED = 0;
    private static final int CTA_IMAGE_CLICKED = 1;
    private static final int ANIMATED_LOGO_CLICKED = 2;

    private static MostVisitedSites sMostVisitedSitesForTests;

    private final Tab mTab;
    private final TabModelSelector mTabModelSelector;
    private final Activity mActivity;

    private final Profile mProfile;
    private final String mTitle;
    private final int mBackgroundColor;
    private final int mThemeColor;
    private final NewTabPageView mNewTabPageView;

    private TabObserver mTabObserver;
    private MostVisitedSites mMostVisitedSites;
    private SnackbarController mMostVisitedItemRemovedController;
    private FaviconHelper mFaviconHelper;
    private LargeIconBridge mLargeIconBridge;
    private LogoBridge mLogoBridge;
    private boolean mSearchProviderHasLogo;
    private String mOnLogoClickUrl;
    private String mAnimatedLogoUrl;
    private FakeboxDelegate mFakeboxDelegate;
    private SnippetsBridge mSnippetsBridge;

    // The timestamp at which the constructor was called.
    private final long mConstructedTimeNs;

    // The timestamp at which this NTP was last shown to the user.
    private long mLastShownTimeNs;

    private boolean mIsLoaded;

    // Whether destroy() has been called.
    private boolean mIsDestroyed;

    /**
     * Allows clients to listen for updates to the scroll changes of the search box on the
     * NTP.
     */
    public interface OnSearchBoxScrollListener {
        /**
         * Callback to be notified when the scroll position of the search box on the NTP has
         * changed.  A scroll percentage of 0, means the search box has no scroll applied and
         * is in it's natural resting position.  A value of 1 means the search box is scrolled
         * entirely to the top of the screen viewport.
         *
         * @param scrollPercentage The percentage the search box has been scrolled off the page.
         */
        void onNtpScrollChanged(float scrollPercentage);
    }

    /**
     * Handles user interaction with the fakebox (the URL bar in the NTP).
     */
    public interface FakeboxDelegate {
        /**
         * Shows the voice recognition dialog. Called when the user taps the microphone icon.
         */
        void startVoiceRecognition();

        /**
         * @return Whether voice search is currently enabled.
         */
        boolean isVoiceSearchEnabled();

        /**
         * Focuses the URL bar when the user taps the fakebox, types in the fakebox, or pastes text
         * into the fakebox.
         *
         * @param pastedText The text that was pasted or typed into the fakebox, or null if the user
         *                   just tapped the fakebox.
         */
        void requestUrlFocusFromFakebox(String pastedText);
    }

    /**
     * @param url The URL to check whether it is for the NTP.
     * @return Whether the passed in URL is used to render the NTP.
     */
    public static boolean isNTPUrl(String url) {
        // Also handle the legacy chrome://newtab URL since that will redirect to
        // chrome-native://newtab natively.
        return url != null
                && (url.startsWith(UrlConstants.NTP_URL) || url.startsWith("chrome://newtab"));
    }

    public static void launchInterestsDialog(Activity activity, final Tab tab) {
        InterestsPage page =
                new InterestsPage(activity, tab, Profile.getLastUsedProfile());
        final Dialog dialog = new NativePageDialog(activity, page);

        InterestsClickListener listener = new InterestsClickListener() {
            @Override
            public void onInterestClicked(String name) {
                tab.loadUrl(new LoadUrlParams(
                        TemplateUrlService.getInstance().getUrlForSearchQuery(name)));
                dialog.dismiss();
            }
        };

        page.setListener(listener);
        dialog.show();
    }

    @VisibleForTesting
    static void setMostVisitedSitesForTests(MostVisitedSites mostVisitedSitesForTests) {
        sMostVisitedSitesForTests = mostVisitedSitesForTests;
    }

    private final NewTabPageManager mNewTabPageManager = new NewTabPageManager() {
        private static final String NTP_OFFLINE_PAGES_FEATURE_NAME = "NTPOfflinePages";

        private boolean isNtpOfflinePagesEnabled() {
            // TODO(treib): Figure out correct offline feature to check.
            return OfflinePageBridge.isOfflinePagesEnabled()
                    && ChromeFeatureList.isEnabled(NTP_OFFLINE_PAGES_FEATURE_NAME);
        }

        @Override
        public boolean isLocationBarShownInNTP() {
            if (mIsDestroyed) return false;
            Context context = mNewTabPageView.getContext();
            return isInSingleUrlBarMode(context)
                    && !mNewTabPageView.urlFocusAnimationsDisabled();
        }

        @Override
        public boolean isVoiceSearchEnabled() {
            return mFakeboxDelegate != null && mFakeboxDelegate.isVoiceSearchEnabled();
        }

        @Override
        public boolean isInterestsEnabled() {
            return CommandLine.getInstance().hasSwitch(ChromeSwitches.ENABLE_INTERESTS)
                    && ChromeSigninController.get(mActivity).isSignedIn();
        }

        @Override
        public boolean isToolbarEnabled() {
            return ChromeFeatureList.isEnabled(ChromeFeatureList.NTP_TOOLBAR)
                    && !SnippetsConfig.isEnabled();
        }

        @Override
        public boolean isFakeOmniboxTextEnabledTablet() {
            return ChromeFeatureList.isEnabled(ChromeFeatureList.NTP_FAKE_OMNIBOX_TEXT);
        }

        private void recordOpenedMostVisitedItem(MostVisitedItem item) {
            if (mIsDestroyed) return;
            NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_MOST_VISITED_ENTRY);
            NewTabPageUma.recordExplicitUserNavigation(
                    item.getUrl(), NewTabPageUma.RAPPOR_ACTION_VISITED_SUGGESTED_TILE);
            RecordHistogram.recordMediumTimesHistogram("NewTabPage.MostVisitedTime",
                    System.nanoTime() - mLastShownTimeNs, TimeUnit.NANOSECONDS);
            mMostVisitedSites.recordOpenedMostVisitedItem(item.getIndex(), item.getTileType());
        }

        @Override
        public void openMostVisitedItem(MostVisitedItem item) {
            if (mIsDestroyed) return;
            recordOpenedMostVisitedItem(item);
            String url = item.getUrl();
            if (!switchToExistingTab(url)) {
                mTab.loadUrl(new LoadUrlParams(url, PageTransition.AUTO_BOOKMARK));
            }
        }

        @TargetApi(Build.VERSION_CODES.LOLLIPOP)
        private boolean switchToExistingTab(String url) {
            String matchPattern = CommandLine.getInstance().getSwitchValue(
                    ChromeSwitches.NTP_SWITCH_TO_EXISTING_TAB);
            boolean matchByHost;
            if ("url".equals(matchPattern)) {
                matchByHost = false;
            } else if ("host".equals(matchPattern)) {
                matchByHost = true;
            } else {
                return false;
            }

            TabModel tabModel = mTabModelSelector.getModel(false);
            for (int i = tabModel.getCount() - 1; i >= 0; --i) {
                if (matchURLs(tabModel.getTabAt(i).getUrl(), url, matchByHost)) {
                    TabModelUtils.setIndex(tabModel, i);
                    return true;
                }
            }
            return false;
        }

        private boolean matchURLs(String url1, String url2, boolean matchByHost) {
            if (url1 == null || url2 == null) return false;
            return matchByHost ? UrlUtilities.sameHost(url1, url2) : url1.equals(url2);
        }

        @Override
        public void openSnippet(String url) {
            openUrl(url);
            NewTabPageUma.monitorVisit(mTab);
        }

        @Override
        public void openUrl(String url) {
            assert !mIsDestroyed;
            mTab.loadUrl(new LoadUrlParams(url, PageTransition.AUTO_BOOKMARK));
        }

        @Override
        public void onCreateContextMenu(ContextMenu menu, OnMenuItemClickListener listener) {
            if (mIsDestroyed) return;
            if (MultiWindowUtils.getInstance().isOpenInOtherWindowSupported(mActivity)) {
                menu.add(Menu.NONE, ID_OPEN_IN_NEW_WINDOW, Menu.NONE,
                        R.string.contextmenu_open_in_other_window)
                        .setOnMenuItemClickListener(listener);
            }
            menu.add(Menu.NONE, ID_OPEN_IN_NEW_TAB, Menu.NONE, R.string.contextmenu_open_in_new_tab)
                    .setOnMenuItemClickListener(listener);
            if (PrefServiceBridge.getInstance().isIncognitoModeEnabled()) {
                menu.add(Menu.NONE, ID_OPEN_IN_INCOGNITO_TAB, Menu.NONE,
                        R.string.contextmenu_open_in_incognito_tab).setOnMenuItemClickListener(
                        listener);
            }
            menu.add(Menu.NONE, ID_REMOVE, Menu.NONE, R.string.remove)
                    .setOnMenuItemClickListener(listener);
        }

        @Override
        public boolean onMenuItemClick(int menuId, MostVisitedItem item) {
            if (mIsDestroyed) return false;
            switch (menuId) {
                case ID_OPEN_IN_NEW_WINDOW:
                    TabDelegate tabDelegate = new TabDelegate(false);
                    LoadUrlParams loadUrlParams = new LoadUrlParams(item.getUrl());
                    tabDelegate.createTabInOtherWindow(loadUrlParams, mActivity,
                            mTab.getParentId());
                    return true;
                case ID_OPEN_IN_NEW_TAB:
                    recordOpenedMostVisitedItem(item);
                    mTabModelSelector.openNewTab(
                            new LoadUrlParams(item.getUrl(), PageTransition.AUTO_BOOKMARK),
                            TabLaunchType.FROM_LONGPRESS_BACKGROUND, mTab, false);
                    return true;
                case ID_OPEN_IN_INCOGNITO_TAB:
                    recordOpenedMostVisitedItem(item);
                    mTabModelSelector.openNewTab(
                            new LoadUrlParams(item.getUrl(), PageTransition.AUTO_BOOKMARK),
                            TabLaunchType.FROM_LONGPRESS_FOREGROUND, mTab, true);
                    return true;
                case ID_REMOVE:
                    mMostVisitedSites.addBlacklistedUrl(item.getUrl());
                    showMostVisitedItemRemovedSnackbar(item.getUrl());
                    return true;
                default:
                    return false;
            }
        }

        @Override
        public void navigateToBookmarks() {
            if (mIsDestroyed) return;
            RecordUserAction.record("MobileNTPSwitchToBookmarks");
            BookmarkUtils.showBookmarkManager(mActivity);
        }

        @Override
        public void navigateToRecentTabs() {
            if (mIsDestroyed) return;
            RecordUserAction.record("MobileNTPSwitchToOpenTabs");
            mTab.loadUrl(new LoadUrlParams(UrlConstants.RECENT_TABS_URL));
        }

        @Override
        public void navigateToInterests() {
            if (mIsDestroyed) return;
            RecordUserAction.record("MobileNTP.Interests.OpenDialog");
            // TODO(peconn): Make this load a native page on tablets.
            launchInterestsDialog(mActivity, mTab);
        }

        @Override
        public void focusSearchBox(boolean beginVoiceSearch, String pastedText) {
            if (mIsDestroyed) return;
            if (mFakeboxDelegate != null) {
                if (beginVoiceSearch) {
                    mFakeboxDelegate.startVoiceRecognition();
                } else {
                    mFakeboxDelegate.requestUrlFocusFromFakebox(pastedText);
                }
            }
        }

        @Override
        public void setMostVisitedURLsObserver(MostVisitedURLsObserver observer, int numResults) {
            if (mIsDestroyed) return;
            mMostVisitedSites.setMostVisitedURLsObserver(observer, numResults);
        }

        @Override
        public void getLocalFaviconImageForURL(
                String url, int size, FaviconImageCallback faviconCallback) {
            if (mIsDestroyed) return;
            if (mFaviconHelper == null) mFaviconHelper = new FaviconHelper();
            mFaviconHelper.getLocalFaviconImageForURL(mProfile, url, size, faviconCallback);
        }

        @Override
        public void getLargeIconForUrl(String url, int size, LargeIconCallback callback) {
            if (mIsDestroyed) return;
            if (mLargeIconBridge == null) mLargeIconBridge = new LargeIconBridge(mProfile);
            mLargeIconBridge.getLargeIconForUrl(url, size, callback);
        }

        @Override
        public void ensureIconIsAvailable(String pageUrl, String iconUrl, boolean isLargeIcon,
                boolean isTemporary, IconAvailabilityCallback callback) {
            if (mIsDestroyed) return;
            if (mFaviconHelper == null) mFaviconHelper = new FaviconHelper();
            mFaviconHelper.ensureIconIsAvailable(mProfile, mTab.getWebContents(), pageUrl, iconUrl,
                    isLargeIcon, isTemporary, callback);
        }

        private boolean isLocalUrl(String url) {
            return "file".equals(Uri.parse(url).getScheme());
        }

        @Override
        public void getUrlsAvailableOffline(
                Set<String> pageUrls, final Callback<Set<String>> callback) {
            final Set<String> urlsAvailableOffline = new HashSet<>();
            if (mIsDestroyed || !isNtpOfflinePagesEnabled()) {
                callback.onResult(urlsAvailableOffline);
                return;
            }

            HashSet<String> urlsToCheckForOfflinePage = new HashSet<>();

            for (String pageUrl : pageUrls) {
                if (isLocalUrl(pageUrl)) {
                    urlsAvailableOffline.add(pageUrl);
                } else {
                    urlsToCheckForOfflinePage.add(pageUrl);
                }
            }

            final long offlineQueryStartTime = SystemClock.elapsedRealtime();

            OfflinePageBridge offlinePageBridge = OfflinePageBridge.getForProfile(mProfile);

            // TODO(dewittj): Remove this code by making the NTP badging available after the NTP is
            // fully loaded.
            if (offlinePageBridge == null || !offlinePageBridge.isOfflinePageModelLoaded()) {
                // Posting a task to avoid potential re-entrancy issues.
                ThreadUtils.postOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        callback.onResult(urlsAvailableOffline);
                    }
                });
                return;
            }

            offlinePageBridge.checkPagesExistOffline(
                    urlsToCheckForOfflinePage, new Callback<Set<String>>() {
                        @Override
                        public void onResult(Set<String> urlsWithOfflinePages) {
                            urlsAvailableOffline.addAll(urlsWithOfflinePages);
                            callback.onResult(urlsAvailableOffline);
                            RecordHistogram.recordTimesHistogram("NewTabPage.OfflineUrlsLoadTime",
                                    SystemClock.elapsedRealtime() - offlineQueryStartTime,
                                    TimeUnit.MILLISECONDS);
                        }
                    });
        }

        @Override
        public void onLogoClicked(boolean isAnimatedLogoShowing) {
            if (mIsDestroyed) return;

            if (!isAnimatedLogoShowing && mAnimatedLogoUrl != null) {
                RecordHistogram.recordSparseSlowlyHistogram(LOGO_CLICK_UMA_NAME, CTA_IMAGE_CLICKED);
                mNewTabPageView.showLogoLoadingView();
                mLogoBridge.getAnimatedLogo(new LogoBridge.AnimatedLogoCallback() {
                    @Override
                    public void onAnimatedLogoAvailable(BaseGifImage animatedLogoImage) {
                        if (mIsDestroyed) return;
                        mNewTabPageView.playAnimatedLogo(animatedLogoImage);
                    }
                }, mAnimatedLogoUrl);
            } else if (mOnLogoClickUrl != null) {
                RecordHistogram.recordSparseSlowlyHistogram(LOGO_CLICK_UMA_NAME,
                        isAnimatedLogoShowing ? ANIMATED_LOGO_CLICKED : STATIC_LOGO_CLICKED);
                mTab.loadUrl(new LoadUrlParams(mOnLogoClickUrl, PageTransition.LINK));
            }
        }

        @Override
        public void getSearchProviderLogo(final LogoObserver logoObserver) {
            if (mIsDestroyed) return;
            LogoObserver wrapperCallback = new LogoObserver() {
                @Override
                public void onLogoAvailable(Logo logo, boolean fromCache) {
                    if (mIsDestroyed) return;
                    mOnLogoClickUrl = logo != null ? logo.onClickUrl : null;
                    mAnimatedLogoUrl = logo != null ? logo.animatedLogoUrl : null;
                    if (logo != null) {
                        RecordHistogram.recordSparseSlowlyHistogram(LOGO_SHOWN_UMA_NAME,
                                logo.animatedLogoUrl == null ? STATIC_LOGO_SHOWN : CTA_IMAGE_SHOWN);
                    }
                    logoObserver.onLogoAvailable(logo, fromCache);
                }
            };
            mLogoBridge.getCurrentLogo(wrapperCallback);
        }

        @Override
        public void onLoadingComplete(MostVisitedItem[] items) {
            if (mIsDestroyed) return;

            long loadTimeMs = (System.nanoTime() - mConstructedTimeNs) / 1000000;
            RecordHistogram.recordTimesHistogram(
                    "Tab.NewTabOnload", loadTimeMs, TimeUnit.MILLISECONDS);
            mIsLoaded = true;
            StartupMetrics.getInstance().recordOpenedNTP();
            NewTabPageUma.recordNTPImpression(NewTabPageUma.NTP_IMPRESSION_REGULAR);
            // If not visible when loading completes, wait until onShown is received.
            if (!mTab.isHidden()) recordNTPShown();

            int tileTypes[] = new int[items.length];
            int sources[] = new int[items.length];
            int providerIndices[] = new int[items.length];

            for (int i = 0; i < items.length; i++) {
                tileTypes[i] = items[i].getTileType();
                sources[i] = items[i].getSource();
                providerIndices[i] = items[i].getProviderIndex();
            }

            mMostVisitedSites.recordTileTypeMetrics(tileTypes, sources, providerIndices);

            if (isNtpOfflinePagesEnabled()) {
                final int maxNumTiles = 12;
                for (int i = 0; i < items.length; i++) {
                    if (items[i].isOfflineAvailable()) {
                        RecordHistogram.recordEnumeratedHistogram(
                                "NewTabPage.TileOfflineAvailable", i, maxNumTiles);
                    }
                }
            }
            SyncSessionsMetrics.recordYoungestForeignTabAgeOnNTP();
        }
    };

    /**
     * Constructs a NewTabPage.
     * @param activity The activity used for context to create the new tab page's View.
     * @param tab The Tab that is showing this new tab page.
     * @param tabModelSelector The TabModelSelector used to open tabs.
     */
    public NewTabPage(Activity activity, Tab tab, TabModelSelector tabModelSelector) {
        mConstructedTimeNs = System.nanoTime();

        mTab = tab;
        mActivity = activity;
        mTabModelSelector = tabModelSelector;
        mProfile = tab.getProfile();

        mTitle = activity.getResources().getString(R.string.button_new_tab);
        mBackgroundColor = NtpColorUtils.getBackgroundColorResource(activity.getResources(), false);
        mThemeColor = ApiCompatibilityUtils.getColor(
                activity.getResources(), R.color.default_primary_color);
        TemplateUrlService.getInstance().addObserver(this);

        mTabObserver = new EmptyTabObserver() {
            @Override
            public void onShown(Tab tab) {
                // Showing the NTP is only meaningful when the page has been loaded already.
                if (mIsLoaded) recordNTPShown();
            }

            @Override
            public void onHidden(Tab tab) {
                if (mIsLoaded) recordNTPInteractionTime();
            }
        };
        mTab.addObserver(mTabObserver);
        mMostVisitedSites = buildMostVisitedSites(mProfile);
        mLogoBridge = new LogoBridge(mProfile);
        updateSearchProviderHasLogo();

        if (SnippetsConfig.isEnabled()) {
            mSnippetsBridge = new SnippetsBridge(mProfile);
        }

        LayoutInflater inflater = LayoutInflater.from(activity);
        mNewTabPageView = (NewTabPageView) inflater.inflate(R.layout.new_tab_page_view, null);
        mNewTabPageView.initialize(mNewTabPageManager, mSearchProviderHasLogo, mSnippetsBridge);

        RecordHistogram.recordBooleanHistogram(
                "NewTabPage.MobileIsUserOnline", NetworkChangeNotifier.isOnline());
    }

    private static MostVisitedSites buildMostVisitedSites(Profile profile) {
        if (sMostVisitedSitesForTests != null) {
            return sMostVisitedSitesForTests;
        } else {
            return new MostVisitedSites(profile);
        }
    }

    private void showMostVisitedItemRemovedSnackbar(String url) {
        if (mMostVisitedItemRemovedController == null) {
            mMostVisitedItemRemovedController = new SnackbarController() {
                @Override
                public void onDismissNoAction(Object actionData) {}

                /** Undoes the most visited item removal. */
                @Override
                public void onAction(Object actionData) {
                    if (mIsDestroyed) return;
                    String url = (String) actionData;
                    mMostVisitedSites.removeBlacklistedUrl(url);
                }
            };
        }
        Context context = mNewTabPageView.getContext();
        Snackbar snackbar = Snackbar
                .make(context.getString(R.string.most_visited_item_removed),
                        mMostVisitedItemRemovedController, Snackbar.TYPE_ACTION,
                        Snackbar.UMA_NTP_MOST_VISITED_DELETE_UNDO)
                .setAction(context.getString(R.string.undo), url);
        mTab.getSnackbarManager().showSnackbar(snackbar);
    }

    /** @return The view container for the new tab page. */
    @VisibleForTesting
    NewTabPageView getNewTabPageView() {
        return mNewTabPageView;
    }

    /** @return whether the NTP is using the cards UI. */
    public boolean isCardsUiEnabled() {
        return SnippetsConfig.isEnabled();
    }

    /**
     * Updates whether the NewTabPage should animate on URL focus changes.
     * @param disable Whether to disable the animations.
     */
    public void setUrlFocusAnimationsDisabled(boolean disable) {
        mNewTabPageView.setUrlFocusAnimationsDisabled(disable);
    }

    private boolean isInSingleUrlBarMode(Context context) {
        if (DeviceFormFactor.isTablet(context)) return false;

        return mSearchProviderHasLogo;
    }

    private void updateSearchProviderHasLogo() {
        mSearchProviderHasLogo = TemplateUrlService.getInstance().isDefaultSearchEngineGoogle();
    }

    private void onSearchEngineUpdated() {
        // TODO(newt): update this if other search providers provide logos.
        updateSearchProviderHasLogo();
        mNewTabPageView.setSearchProviderHasLogo(mSearchProviderHasLogo);
    }

    /**
     * Specifies the percentage the URL is focused during an animation.  1.0 specifies that the URL
     * bar has focus and has completed the focus animation.  0 is when the URL bar is does not have
     * any focus.
     *
     * @param percent The percentage of the URL bar focus animation.
     */
    public void setUrlFocusChangeAnimationPercent(float percent) {
        mNewTabPageView.setUrlFocusChangeAnimationPercent(percent);
    }

    /**
     * Get the bounds of the search box in relation to the top level NewTabPage view.
     *
     * @param originalBounds The bounding region of the search box without external transforms
     *                       applied.  The delta between this and the transformed bounds determines
     *                       the amount of scroll applied to this view.
     * @param transformedBounds The bounding region of the search box including any transforms
     *                          applied by the parent view hierarchy up to the NewTabPage view.
     *                          This more accurately reflects the current drawing location of the
     *                          search box.
     */
    public void getSearchBoxBounds(Rect originalBounds, Rect transformedBounds) {
        mNewTabPageView.getSearchBoxBounds(originalBounds, transformedBounds);
    }

    /**
     * @return Whether the location bar is shown in the NTP.
     */
    public boolean isLocationBarShownInNTP() {
        return mNewTabPageManager.isLocationBarShownInNTP();
    }

    /**
     * Sets the listener for search box scroll changes.
     * @param listener The listener to be notified on changes.
     */
    public void setSearchBoxScrollListener(OnSearchBoxScrollListener listener) {
        mNewTabPageView.setSearchBoxScrollListener(listener);
    }

    /**
     * Sets the FakeboxDelegate that this pages interacts with.
     */
    public void setFakeboxDelegate(FakeboxDelegate fakeboxDelegate) {
        mFakeboxDelegate = fakeboxDelegate;
        if (mFakeboxDelegate != null) {
            mNewTabPageView.updateVoiceSearchButtonVisibility();
        }
    }

    /**
     * Records UMA for the NTP being shown. This includes a fresh page load or being brought to the
     * foreground.
     */
    private void recordNTPShown() {
        mLastShownTimeNs = System.nanoTime();
        RecordUserAction.record("MobileNTPShown");
    }

    private void recordNTPInteractionTime() {
        RecordHistogram.recordMediumTimesHistogram(
                "NewTabPage.TimeSpent", System.nanoTime() - mLastShownTimeNs, TimeUnit.NANOSECONDS);
    }

    /**
     * @return Whether the NTP has finished loaded.
     */
    @VisibleForTesting
    public boolean isLoadedForTests() {
        return mIsLoaded;
    }

    // TemplateUrlServiceObserver overrides

    @Override
    public void onTemplateURLServiceChanged() {
        onSearchEngineUpdated();
    }

    // NativePage overrides

    @Override
    public void destroy() {
        assert !mIsDestroyed;
        assert getView().getParent() == null : "Destroy called before removed from window";
        if (mIsLoaded && !mTab.isHidden()) recordNTPInteractionTime();

        if (mFaviconHelper != null) {
            mFaviconHelper.destroy();
            mFaviconHelper = null;
        }
        if (mLargeIconBridge != null) {
            mLargeIconBridge.destroy();
            mLargeIconBridge = null;
        }
        if (mMostVisitedSites != null) {
            mMostVisitedSites.destroy();
            mMostVisitedSites = null;
        }
        if (mLogoBridge != null) {
            mLogoBridge.destroy();
            mLogoBridge = null;
        }
        if (mSnippetsBridge != null) {
            mSnippetsBridge.destroy();
            mSnippetsBridge = null;
        }
        if (mMostVisitedItemRemovedController != null) {
            mTab.getSnackbarManager().dismissSnackbars(mMostVisitedItemRemovedController);
        }
        TemplateUrlService.getInstance().removeObserver(this);
        mTab.removeObserver(mTabObserver);
        mTabObserver = null;
        mIsDestroyed = true;
    }

    @Override
    public String getUrl() {
        return UrlConstants.NTP_URL;
    }

    @Override
    public String getTitle() {
        return mTitle;
    }

    @Override
    public int getBackgroundColor() {
        return mBackgroundColor;
    }

    @Override
    public int getThemeColor() {
        return isLocationBarShownInNTP()
                ? NtpColorUtils.getBackgroundColorResource(mActivity.getResources(), false)
                : mThemeColor;
    }

    @Override
    public View getView() {
        return mNewTabPageView;
    }

    @Override
    public String getHost() {
        return UrlConstants.NTP_HOST;
    }

    @Override
    public void updateForUrl(String url) {
    }

    // InvalidationAwareThumbnailProvider

    @Override
    public boolean shouldCaptureThumbnail() {
        return mNewTabPageView.shouldCaptureThumbnail();
    }

    @Override
    public void captureThumbnail(Canvas canvas) {
        mNewTabPageView.captureThumbnail(canvas);
    }
}
