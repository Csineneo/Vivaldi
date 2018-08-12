// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.enhancedbookmarks;

import android.content.Intent;
import android.net.Uri;
import android.test.suitebuilder.annotation.SmallTest;
import android.text.TextUtils;
import android.view.View;

import junit.framework.Assert;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.UrlConstants;
import org.chromium.chrome.browser.bookmark.BookmarksBridge.BookmarkItem;
import org.chromium.chrome.browser.bookmark.BookmarksBridge.BookmarkModelObserver;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.chrome.test.util.ActivityUtils;
import org.chromium.chrome.test.util.BookmarkTestUtils;
import org.chromium.chrome.test.util.ChromeTabUtils;
import org.chromium.chrome.test.util.MenuUtils;
import org.chromium.chrome.test.util.TestHttpServerClient;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.bookmarks.BookmarkType;
import org.chromium.content.browser.test.util.CallbackHelper;
import org.chromium.content.browser.test.util.TouchCommon;
import org.chromium.ui.base.DeviceFormFactor;

import java.util.concurrent.Callable;
import java.util.concurrent.TimeoutException;

/**
 * Tests for the enhanced bookmark manager.
 */
public class EnhancedBookmarkTest extends ChromeActivityTestCaseBase<ChromeActivity> {

    public EnhancedBookmarkTest() {
        super(ChromeActivity.class);
    }

    private static final String TEST_PAGE = TestHttpServerClient.getUrl(
            "chrome/test/data/android/google.html");
    private static final String TEST_PAGE_TITLE = "The Google";

    private EnhancedBookmarksModel mBookmarkModel;
    protected EnhancedBookmarkRecyclerView mItemsContainer;

    @Override
    public void startMainActivity() throws InterruptedException {
        startMainActivityFromLauncher();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mBookmarkModel = new EnhancedBookmarksModel(
                        getActivity().getActivityTab().getProfile());
            }
        });
        waitForBookmarkModelLoaded();
    }

    private void waitForBookmarkModelLoaded() throws InterruptedException {
        final CallbackHelper loadedCallback = new CallbackHelper();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                if (mBookmarkModel.isBookmarkModelLoaded()) loadedCallback.notifyCalled();
                else {
                    mBookmarkModel.addObserver(new BookmarkModelObserver() {
                        @Override
                        public void bookmarkModelChanged() {}

                        @Override
                        public void bookmarkModelLoaded() {
                            loadedCallback.notifyCalled();
                            mBookmarkModel.removeObserver(this);
                        }
                    });
                }
            }
        });
        try {
            loadedCallback.waitForCallback(0);
        } catch (TimeoutException e) {
            Assert.fail("Enhanced Bookmark model did not load: Timeout.");
        }
    }

    private void openBookmarkManager() throws InterruptedException {
        if (DeviceFormFactor.isTablet(getActivity())) {
            loadUrl(UrlConstants.BOOKMARKS_URL);
            mItemsContainer = (EnhancedBookmarkRecyclerView) getActivity().findViewById(
                    R.id.eb_items_container);
        } else {
            // phone
            EnhancedBookmarkActivity activity = ActivityUtils.waitForActivity(getInstrumentation(),
                    EnhancedBookmarkActivity.class, new MenuUtils.MenuActivityTrigger(
                            getInstrumentation(), getActivity(), R.id.all_bookmarks_menu_id));
            mItemsContainer = (EnhancedBookmarkRecyclerView) activity.findViewById(
                    R.id.eb_items_container);
        }
    }

    private void openBookmarkManager(final String url) throws InterruptedException {
        if (DeviceFormFactor.isTablet(getActivity())) {
            loadUrl(url);
            mItemsContainer = (EnhancedBookmarkRecyclerView) getActivity().findViewById(
                    R.id.eb_items_container);
        } else {
            // phone
            EnhancedBookmarkActivity activity = ActivityUtils.waitForActivity(getInstrumentation(),
                    EnhancedBookmarkActivity.class, new Runnable() {
                        @Override
                        public void run() {
                            Intent intent = new Intent(getActivity(),
                                    EnhancedBookmarkActivity.class);
                            intent.setData(Uri.parse(url));
                            getActivity().startActivity(intent);
                        }
                    });
            mItemsContainer = (EnhancedBookmarkRecyclerView) activity.findViewById(
                    R.id.eb_items_container);
        }
    }

    private boolean isItemPresentInBookmarkList(final String expectedTitle) {
        return ThreadUtils.runOnUiThreadBlockingNoException(new Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                for (int i = 0; i < mItemsContainer.getAdapter().getItemCount(); i++) {
                    BookmarkId item = mItemsContainer.getAdapter().getItem(i);
                    if (item == null) continue;

                    String actualTitle = mBookmarkModel.getBookmarkTitle(item);
                    if (TextUtils.equals(actualTitle, expectedTitle)) {
                        return true;
                    }
                }
                return false;
            }
        });
    }

    @SmallTest
    public void testAddBookmark() throws InterruptedException {
        loadUrl(TEST_PAGE);
        // Click star button to bookmark the curent tab.
        MenuUtils.invokeCustomMenuActionSync(getInstrumentation(), getActivity(),
                R.id.bookmark_this_page_id);
        // All actions with EnhancedBookmarksModel needs to run on UI thread.
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                long bookmarkIdLong = getActivity().getActivityTab().getUserBookmarkId();
                BookmarkId id = new BookmarkId(bookmarkIdLong, BookmarkType.NORMAL);
                assertTrue("The test page is not added as bookmark: ",
                        mBookmarkModel.doesBookmarkExist(id));
                BookmarkItem item = mBookmarkModel.getBookmarkById(id);
                assertEquals(mBookmarkModel.getDefaultFolder(), item.getParentId());
                assertEquals(TEST_PAGE, item.getUrl());
                assertEquals(TEST_PAGE_TITLE, item.getTitle());
            }
        });
    }

    @SmallTest
    public void testOpenBookmark() throws InterruptedException {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mBookmarkModel.addBookmark(mBookmarkModel.getDefaultFolder(), 0, TEST_PAGE_TITLE,
                        TEST_PAGE);
            }
        });
        openBookmarkManager();
        assertTrue("Grid view does not contain added bookmark: ",
                isItemPresentInBookmarkList(TEST_PAGE_TITLE));
        final View tile = BookmarkTestUtils.getViewWithText(mItemsContainer, TEST_PAGE_TITLE);
        ChromeTabUtils.waitForTabPageLoaded(getActivity().getActivityTab(), new Runnable() {
            @Override
            public void run() {
                TouchCommon.singleClickView(tile);
            }
        });
        assertEquals(TEST_PAGE_TITLE, getActivity().getActivityTab().getTitle());
    }

    @SmallTest
    public void testUrlComposition() {
        BookmarkId mobileId = mBookmarkModel.getMobileFolderId();
        BookmarkId bookmarkBarId = mBookmarkModel.getDesktopFolderId();
        BookmarkId otherId = mBookmarkModel.getOtherFolderId();
        assertEquals("chrome-native://bookmarks/folder/" + mobileId,
                EnhancedBookmarkUIState.createFolderUrl(mobileId).toString());
        assertEquals("chrome-native://bookmarks/folder/" + bookmarkBarId,
                EnhancedBookmarkUIState.createFolderUrl(bookmarkBarId).toString());
        assertEquals("chrome-native://bookmarks/folder/" + otherId,
                EnhancedBookmarkUIState.createFolderUrl(otherId).toString());

        assertEquals("chrome-native://bookmarks/filter/OFFLINE_PAGES", EnhancedBookmarkUIState
                .createFilterUrl(EnhancedBookmarkFilter.OFFLINE_PAGES, true).toString());
        assertEquals(
                "chrome-native://bookmarks/filter/OFFLINE_PAGES?persist=0",
                EnhancedBookmarkUIState.createFilterUrl(EnhancedBookmarkFilter.OFFLINE_PAGES,
                        false).toString());
    }

    @SmallTest
    public void testOpenBookmarkManager() throws InterruptedException {
        openBookmarkManager();
        EnhancedBookmarkDelegate delegate = mItemsContainer.getDelegateForTesting();
        assertEquals(EnhancedBookmarkUIState.STATE_ALL_BOOKMARKS, delegate.getCurrentState());
        assertEquals(UrlConstants.BOOKMARKS_URL,
                EnhancedBookmarkUtils.getLastUsedUrl(getActivity()));
    }

    @SmallTest
    @CommandLineFlags.Add(ChromeSwitches.ENABLE_OFFLINE_PAGES)
    public void testOpenBookmarkManagerInOfflinePagePersist() throws InterruptedException {
        EnhancedBookmarkUtils.setLastUsedUrl(getActivity(), UrlConstants.BOOKMARKS_URL);
        String url = "chrome-native://bookmarks/filter/OFFLINE_PAGES";
        openBookmarkManager(url);
        EnhancedBookmarkDelegate delegate = mItemsContainer.getDelegateForTesting();
        assertEquals(EnhancedBookmarkUIState.STATE_FILTER, delegate.getCurrentState());
        assertEquals(url, EnhancedBookmarkUtils.getLastUsedUrl(getActivity()));
    }

    @SmallTest
    @CommandLineFlags.Add(ChromeSwitches.ENABLE_OFFLINE_PAGES)
    public void testOpenBookmarkManagerInOfflinePageNoPersist() throws InterruptedException {
        EnhancedBookmarkUtils.setLastUsedUrl(getActivity(), UrlConstants.BOOKMARKS_URL);
        String url = "chrome-native://bookmarks/filter/OFFLINE_PAGES?persist=0";
        openBookmarkManager(url);
        EnhancedBookmarkDelegate delegate = mItemsContainer.getDelegateForTesting();
        assertEquals(EnhancedBookmarkUIState.STATE_FILTER, delegate.getCurrentState());
        assertEquals(UrlConstants.BOOKMARKS_URL,
                EnhancedBookmarkUtils.getLastUsedUrl(getActivity()));
    }

    @SmallTest
    @CommandLineFlags.Add(ChromeSwitches.DISABLE_OFFLINE_PAGES)
    public void testOpenBookmarkManagerInOfflinePageWhenDisabled() throws InterruptedException {
        openBookmarkManager("chrome-native://bookmarks/filter/OFFLINE_PAGES");
        EnhancedBookmarkDelegate delegate = mItemsContainer.getDelegateForTesting();
        assertEquals(EnhancedBookmarkUIState.STATE_ALL_BOOKMARKS, delegate.getCurrentState());
    }
}
