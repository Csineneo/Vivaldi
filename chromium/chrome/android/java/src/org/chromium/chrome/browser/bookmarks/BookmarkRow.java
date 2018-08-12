// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarks;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnLongClickListener;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.Checkable;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.ListPopupWindow;
import android.widget.TextView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.bookmarks.BookmarkBridge.BookmarkItem;
import org.chromium.chrome.browser.widget.TintedImageButton;
import org.chromium.components.bookmarks.BookmarkId;

import java.util.List;

/**
 * Common logic for bookmark and folder rows.
 */
abstract class BookmarkRow extends FrameLayout implements BookmarkUIObserver,
        Checkable, OnClickListener, OnLongClickListener {

    protected ImageView mIconImageView;
    protected TextView mTitleView;
    protected TintedImageButton mMoreIcon;
    private BookmarkItemHighlightView mHighlightView;

    protected BookmarkDelegate mDelegate;
    protected BookmarkId mBookmarkId;
    private ListPopupWindow mPopupMenu;
    private boolean mIsAttachedToWindow = false;

    /**
     * Constructor for inflating from XML.
     */
    public BookmarkRow(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    /**
     * Updates this row for the given {@link BookmarkId}.
     * @return The {@link BookmarkItem} corresponding the given {@link BookmarkId}.
     */
    BookmarkItem setBookmarkId(BookmarkId bookmarkId) {
        mBookmarkId = bookmarkId;
        BookmarkItem bookmarkItem = mDelegate.getModel().getBookmarkById(bookmarkId);
        clearPopup();
        if (isSelectable()) {
            mMoreIcon.setVisibility(bookmarkItem.isEditable() ? VISIBLE : GONE);
            setChecked(mDelegate.isBookmarkSelected(bookmarkId));
        }
        return bookmarkItem;
    }

    /**
     * Same as {@link OnClickListener#onClick(View)} on this.
     * Subclasses should override this instead of setting their own OnClickListener because this
     * class handles onClick events in selection mode, and won't forward events to subclasses in
     * that case.
     */
    protected abstract void onClick();

    private void initialize() {
        mDelegate.addUIObserver(this);
        updateSelectionState();
    }

    private void clearPopup() {
        if (mPopupMenu != null) {
            mPopupMenu.dismiss();
            mPopupMenu = null;
        }
    }

    private void cleanup() {
        clearPopup();
        if (mDelegate != null) mDelegate.removeUIObserver(this);
    }

    private void updateSelectionState() {
        if (isSelectable()) mMoreIcon.setClickable(!mDelegate.isSelectionEnabled());
    }

    /**
     * @return Whether this row is selectable.
     */
    protected boolean isSelectable() {
        return true;
    }

    /**
     * Show drop-down menu after user click on more-info icon
     * @param view The anchor view for the menu
     */
    private void showMenu(View view) {
        if (mPopupMenu == null) {
            mPopupMenu = new ListPopupWindow(getContext(), null, 0, R.style.BookmarkMenuStyle);
            mPopupMenu.setAdapter(new ArrayAdapter<String>(
                    getContext(), R.layout.bookmark_popup_item, new String[] {
                            getContext().getString(R.string.bookmark_item_select),
                            getContext().getString(R.string.bookmark_item_edit),
                            getContext().getString(R.string.bookmark_item_move),
                            getContext().getString(R.string.bookmark_item_delete)}) {
                private static final int MOVE_POSITION = 2;

                @Override
                public boolean areAllItemsEnabled() {
                    return false;
                }

                @Override
                public boolean isEnabled(int position) {
                    // In some erroneous states, the popup window might hang around even if the
                    // activity is killed (crbug.com/594213), so null check here.
                    if (mDelegate == null || mDelegate.getModel() == null) return false;
                    if (position == MOVE_POSITION) {
                        BookmarkItem bookmark = mDelegate.getModel().getBookmarkById(mBookmarkId);
                        if (bookmark == null) return false;
                        return bookmark.isMovable();
                    }
                    return true;
                }

                @Override
                public View getView(int position, View convertView, ViewGroup parent) {
                    View view = super.getView(position, convertView, parent);
                    view.setEnabled(isEnabled(position));
                    return view;
                }
            });
            mPopupMenu.setAnchorView(view);
            mPopupMenu.setWidth(getResources().getDimensionPixelSize(
                            R.dimen.bookmark_item_popup_width));
            mPopupMenu.setVerticalOffset(-view.getHeight());
            mPopupMenu.setModal(true);
            mPopupMenu.setOnItemClickListener(new OnItemClickListener() {
                @Override
                public void onItemClick(AdapterView<?> parent, View view, int position,
                        long id) {
                    if (position == 0) {
                        setChecked(mDelegate.toggleSelectionForBookmark(mBookmarkId));
                    } else if (position == 1) {
                        BookmarkItem item = mDelegate.getModel().getBookmarkById(mBookmarkId);
                        if (item.isFolder()) {
                            BookmarkAddEditFolderActivity.startEditFolderActivity(
                                    getContext(), item.getId());
                        } else {
                            BookmarkUtils.startEditActivity(getContext(), item.getId());
                        }
                    } else if (position == 2) {
                        BookmarkFolderSelectActivity.startFolderSelectActivity(getContext(),
                                mBookmarkId);
                    } else if (position == 3) {
                        if (mDelegate != null && mDelegate.getModel() != null) {
                            mDelegate.getModel().deleteBookmarks(mBookmarkId);
                        }
                    }
                    // Somehow the on click event can be triggered way after we dismiss the popup.
                    // http://crbug.com/600642
                    if (mPopupMenu != null) mPopupMenu.dismiss();
                }
            });
        }
        mPopupMenu.show();
        mPopupMenu.getListView().setDivider(null);
    }

    // FrameLayout implementations.

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        mIconImageView = (ImageView) findViewById(R.id.bookmark_image);
        mTitleView = (TextView) findViewById(R.id.title);

        if (isSelectable()) {
            mHighlightView = (BookmarkItemHighlightView) findViewById(R.id.highlight);

            mMoreIcon = (TintedImageButton) findViewById(R.id.more);
            mMoreIcon.setVisibility(VISIBLE);
            mMoreIcon.setOnClickListener(new OnClickListener() {
                @Override
                public void onClick(View view) {
                    showMenu(view);
                }
            });
        }

        setOnClickListener(this);
        setOnLongClickListener(this);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        mIsAttachedToWindow = true;
        if (mDelegate != null) {
            setChecked(mDelegate.isBookmarkSelected(mBookmarkId));
            initialize();
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        mIsAttachedToWindow = false;
        cleanup();
        setChecked(false);
    }

    // OnClickListener implementation.

    @Override
    public final void onClick(View view) {
        assert view == this;

        if (mDelegate.isSelectionEnabled() && isSelectable()) {
            onLongClick(view);
        } else {
            onClick();
        }
    }

    // OnLongClickListener implementation.

    @Override
    public boolean onLongClick(View view) {
        assert view == this;
        if (!isSelectable()) return false;
        setChecked(mDelegate.toggleSelectionForBookmark(mBookmarkId));
        return true;
    }

    // Checkable implementations.

    @Override
    public boolean isChecked() {
        if (mHighlightView == null) return false;
        return mHighlightView.isChecked();
    }

    @Override
    public void toggle() {
        setChecked(!isChecked());
    }

    @Override
    public void setChecked(boolean checked) {
        // Unselectable rows do not have highlight view.
        if (mHighlightView != null) mHighlightView.setChecked(checked);
    }

    // BookmarkUIObserver implementations.

    @Override
    public void onBookmarkDelegateInitialized(BookmarkDelegate delegate) {
        assert mDelegate == null;
        mDelegate = delegate;
        if (mIsAttachedToWindow) initialize();
    }

    @Override
    public void onDestroy() {
        cleanup();
    }

    @Override
    public void onAllBookmarksStateSet() {
    }

    @Override
    public void onFolderStateSet(BookmarkId folder) {
    }

    @Override
    public void onSelectionStateChange(List<BookmarkId> selectedBookmarks) {
        updateSelectionState();
    }
}
