// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.cards;

import android.graphics.Canvas;
import android.support.v4.view.animation.FastOutLinearInInterpolator;
import android.support.v7.widget.RecyclerView;
import android.support.v7.widget.RecyclerView.Adapter;
import android.support.v7.widget.RecyclerView.ViewHolder;
import android.support.v7.widget.helper.ItemTouchHelper;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.Interpolator;

import org.chromium.base.Log;
import org.chromium.chrome.browser.ntp.NewTabPageLayout;
import org.chromium.chrome.browser.ntp.NewTabPageView.NewTabPageManager;
import org.chromium.chrome.browser.ntp.snippets.SnippetArticle;
import org.chromium.chrome.browser.ntp.snippets.SnippetArticleViewHolder;
import org.chromium.chrome.browser.ntp.snippets.SnippetHeaderListItem;
import org.chromium.chrome.browser.ntp.snippets.SnippetsBridge.SnippetsObserver;

import java.util.ArrayList;
import java.util.List;

/**
 * A class that handles merging above the fold elements and below the fold cards into an adapter
 * that will be used to back the NTP RecyclerView. The first element in the adapter should always be
 * the above-the-fold view (containing the logo, search box, and most visited tiles) and subsequent
 * elements will be the cards shown to the user
 */
public class NewTabPageAdapter extends Adapter<NewTabPageViewHolder> implements SnippetsObserver {
    private static final String TAG = "Ntp";
    private static final Interpolator FADE_INTERPOLATOR = new FastOutLinearInInterpolator();

    private final NewTabPageManager mNewTabPageManager;
    private final NewTabPageLayout mNewTabPageLayout;
    private final AboveTheFoldListItem mAboveTheFoldListItem;
    private final List<NewTabPageListItem> mNewTabPageListItems;
    private final ItemTouchCallbacks mItemTouchCallbacks;
    private NewTabPageRecyclerView mRecyclerView;
    private boolean mWantsSnippets;

    private class ItemTouchCallbacks extends ItemTouchHelper.Callback {
        @Override
        public void onSwiped(ViewHolder viewHolder, int direction) {
            mRecyclerView.onItemDismissStarted(viewHolder.itemView);

            // This is going to have a effect at the next layout pass, which is going to happen
            // after the item has been removed from the adapter and the layout.
            mRecyclerView.refreshBottomSpacing();

            NewTabPageAdapter.this.dismissItem(viewHolder);
        }

        @Override
        public void clearView(RecyclerView recyclerView, ViewHolder viewHolder) {
            // clearView() is called when an interaction with the item is finished, which does
            // not mean that the user went all the way and dismissed the item before releasing it.
            // We need to check that the item has been removed.
            if (viewHolder.getAdapterPosition() == RecyclerView.NO_POSITION) {
                mRecyclerView.onItemDismissFinished(viewHolder.itemView);
            }

            super.clearView(recyclerView, viewHolder);
        }

        @Override
        public boolean onMove(RecyclerView recyclerView, ViewHolder viewHolder, ViewHolder target) {
            assert false; // Drag and drop not supported, the method will never be called.
            return false;
        }

        @Override
        public int getMovementFlags(RecyclerView recyclerView, ViewHolder viewHolder) {
            assert viewHolder instanceof NewTabPageViewHolder;

            int swipeFlags = 0;
            NewTabPageViewHolder ntpViewHolder = (NewTabPageViewHolder) viewHolder;
            if (ntpViewHolder.isDismissable()) {
                swipeFlags = ItemTouchHelper.START | ItemTouchHelper.END;
            }

            return makeMovementFlags(0 /* dragFlags */, swipeFlags);
        }

        @Override
        public void onChildDraw(Canvas c, RecyclerView recyclerView, ViewHolder viewHolder,
                float dX, float dY, int actionState, boolean isCurrentlyActive) {
            View itemView = viewHolder.itemView;
            float input = Math.abs(dX) / itemView.getMeasuredWidth();
            float alpha = 1 - FADE_INTERPOLATOR.getInterpolation(input);
            itemView.setAlpha(alpha);
            super.onChildDraw(c, recyclerView, viewHolder, dX, dY, actionState, isCurrentlyActive);
        }
    }

    /**
     * Constructor to create the manager for all the cards to display on the NTP
     *
     * @param manager the NewTabPageManager to use to interact with the snippets service and the
     *                rest of the system.
     * @param newTabPageLayout the layout encapsulating all the above-the-fold elements
     *                         (logo, search box, most visited tiles)
     */
    public NewTabPageAdapter(NewTabPageManager manager, NewTabPageLayout newTabPageLayout) {
        mNewTabPageManager = manager;
        mNewTabPageLayout = newTabPageLayout;
        mAboveTheFoldListItem = new AboveTheFoldListItem();
        mItemTouchCallbacks = new ItemTouchCallbacks();
        mNewTabPageListItems = new ArrayList<NewTabPageListItem>();
        mNewTabPageListItems.add(mAboveTheFoldListItem);
        mWantsSnippets = true;
        mNewTabPageManager.setSnippetsObserver(this);
    }

    /** Returns callbacks to configure the interactions with the RecyclerView's items. */
    public ItemTouchHelper.Callback getItemTouchCallbacks() {
        return mItemTouchCallbacks;
    }

    @Override
    public void onSnippetsReceived(List<SnippetArticle> listSnippets) {
        if (!mWantsSnippets) return;

        int newSnippetCount = listSnippets.size();
        Log.d(TAG, "Received %d new snippets.", newSnippetCount);

        // At first, there might be no snippets available, we wait until they have been fetched.
        if (newSnippetCount == 0) return;

        loadSnippets(listSnippets);

        // We don't want to get notified of other changes.
        mWantsSnippets = false;
    }

    @Override
    public void onSnippetsDisabled() {
        // Clear the snippets, wait for new updates in case the service is reenabled later.
        loadSnippets(new ArrayList<SnippetArticle>());
        mWantsSnippets = true;
    }

    @Override
    @NewTabPageListItem.ViewType
    public int getItemViewType(int position) {
        return mNewTabPageListItems.get(position).getType();
    }

    @Override
    public NewTabPageViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        if (viewType == NewTabPageListItem.VIEW_TYPE_ABOVE_THE_FOLD) {
            return new NewTabPageViewHolder(mNewTabPageLayout);
        }

        if (viewType == NewTabPageListItem.VIEW_TYPE_HEADER) {
            return new NewTabPageViewHolder(SnippetHeaderListItem.createView(parent));
        }

        if (viewType == NewTabPageListItem.VIEW_TYPE_SNIPPET) {
            return new SnippetArticleViewHolder(
                    SnippetArticleViewHolder.createView(parent), mNewTabPageManager);
        }

        if (viewType == NewTabPageListItem.VIEW_TYPE_SPACING) {
            return new NewTabPageViewHolder(SpacingListItem.createView(parent));
        }

        return null;
    }

    @Override
    public void onBindViewHolder(NewTabPageViewHolder holder, final int position) {
        holder.onBindViewHolder(mNewTabPageListItems.get(position));
    }

    @Override
    public int getItemCount() {
        return mNewTabPageListItems.size();
    }

    private void loadSnippets(List<SnippetArticle> listSnippets) {
        // Copy thumbnails over
        for (SnippetArticle newSnippet : listSnippets) {
            int existingSnippetIdx = mNewTabPageListItems.indexOf(newSnippet);
            if (existingSnippetIdx == -1) continue;

            newSnippet.setThumbnailBitmap(
                    ((SnippetArticle) mNewTabPageListItems.get(existingSnippetIdx))
                            .getThumbnailBitmap());
        }

        mNewTabPageListItems.clear();
        mNewTabPageListItems.add(mAboveTheFoldListItem);
        mNewTabPageListItems.add(new SnippetHeaderListItem());
        mNewTabPageListItems.addAll(listSnippets);
        mNewTabPageListItems.add(new SpacingListItem());

        notifyDataSetChanged();
    }

    @Override
    public void onAttachedToRecyclerView(RecyclerView recyclerView) {
        super.onAttachedToRecyclerView(recyclerView);

        // We are assuming for now that the adapter is used with a single RecyclerView.
        // Getting the reference as we are doing here is going to be broken if that changes.
        assert mRecyclerView == null;

        // FindBugs chokes on the cast below when not checked, raising BC_UNCONFIRMED_CAST
        assert recyclerView instanceof NewTabPageRecyclerView;

        mRecyclerView = (NewTabPageRecyclerView) recyclerView;
    }

    private void dismissItem(ViewHolder itemViewHolder) {
        assert itemViewHolder.getItemViewType() == NewTabPageListItem.VIEW_TYPE_SNIPPET;

        int position = itemViewHolder.getAdapterPosition();
        mNewTabPageManager.onSnippetDismissed((SnippetArticle) mNewTabPageListItems.get(position));

        mNewTabPageListItems.remove(position);
        notifyItemRemoved(position);
    }

    List<NewTabPageListItem> getItemsForTesting() {
        return mNewTabPageListItems;
    }
}
