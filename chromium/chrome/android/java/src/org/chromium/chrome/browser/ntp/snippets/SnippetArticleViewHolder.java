// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.snippets;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.TransitionDrawable;
import android.media.ThumbnailUtils;
import android.text.format.DateUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.MeasureSpec;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.favicon.FaviconHelper.FaviconImageCallback;
import org.chromium.chrome.browser.ntp.NewTabPageUma;
import org.chromium.chrome.browser.ntp.NewTabPageView.NewTabPageManager;
import org.chromium.chrome.browser.ntp.cards.NewTabPageListItem;
import org.chromium.chrome.browser.ntp.cards.NewTabPageViewHolder;

import java.net.URI;
import java.net.URISyntaxException;

/**
 * A class that represents the view for a single card snippet.
 */
public class SnippetArticleViewHolder extends NewTabPageViewHolder implements View.OnClickListener {
    private static final String TAG = "NtpSnippets";
    private static final String PUBLISHER_FORMAT_STRING = "%s - %s";
    private static final int FADE_IN_ANIMATION_TIME_MS = 300;

    private final NewTabPageManager mNewTabPageManager;
    private final TextView mHeadlineTextView;
    private final TextView mPublisherTextView;
    private final TextView mArticleSnippetTextView;
    private final ImageView mThumbnailView;

    private FetchImageCallback mImageCallback;

    public String mUrl;
    public int mPosition;

    /**
     * Creates the CardView object to display snippets information
     *
     * @param parent The parent view for the card
     * @return a CardView object for displaying snippets
     */
    public static View createView(ViewGroup parent) {
        return LayoutInflater.from(parent.getContext())
                .inflate(R.layout.new_tab_page_snippets_card, parent, false);
    }

    /**
     * Constructs a SnippetCardItemView item used to display snippets
     *
     * @param cardView The View for the snippet card
     * @param manager The SnippetsManager object used to open an article
     */
    public SnippetArticleViewHolder(View cardView, NewTabPageManager manager) {
        super(cardView);

        mNewTabPageManager = manager;
        cardView.setOnClickListener(this);
        mThumbnailView = (ImageView) cardView.findViewById(R.id.article_thumbnail);
        mHeadlineTextView = (TextView) cardView.findViewById(R.id.article_headline);
        mPublisherTextView = (TextView) cardView.findViewById(R.id.article_publisher);
        mArticleSnippetTextView = (TextView) cardView.findViewById(R.id.article_snippet);
        cardView.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener() {
            @Override
            public void onViewAttachedToWindow(View v) {
                RecordHistogram.recordSparseSlowlyHistogram(
                        "NewTabPage.Snippets.CardShown", mPosition);
            }

            @Override
            public void onViewDetachedFromWindow(View v) {}
        });
    }

    @Override
    public void onClick(View v) {
        mNewTabPageManager.open(mUrl);
        RecordUserAction.record("MobileNTP.Snippets.Click");
        RecordHistogram.recordSparseSlowlyHistogram("NewTabPage.Snippets.CardClicked", mPosition);
        NewTabPageUma.recordSnippetAction(NewTabPageUma.SNIPPETS_ACTION_CLICKED);
        NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_SNIPPET);
    }

    @Override
    public void onBindViewHolder(NewTabPageListItem article) {
        SnippetArticle item = (SnippetArticle) article;

        mHeadlineTextView.setText(item.mTitle);
        mPublisherTextView.setText(String.format(PUBLISHER_FORMAT_STRING, item.mPublisher,
                DateUtils.getRelativeTimeSpanString(item.mTimestamp, System.currentTimeMillis(),
                        DateUtils.MINUTE_IN_MILLIS)));

        mArticleSnippetTextView.setText(item.mPreviewText);
        mUrl = item.mUrl;
        mPosition = item.mPosition;

        // If there's still a pending thumbnail fetch, cancel it.
        cancelImageFetch();

        // If the article has a thumbnail already, reuse it. Otherwise start a fetch.
        if (item.getThumbnailBitmap() != null) {
            mThumbnailView.setImageBitmap(item.getThumbnailBitmap());
        } else {
            mThumbnailView.setImageResource(R.drawable.ic_snippet_thumbnail_placeholder);
            mImageCallback = new FetchImageCallback(this, item);
            mNewTabPageManager.fetchSnippetImage(item, mImageCallback);
        }

        updateFavicon(item);
    }

    private static class FetchImageCallback implements SnippetsBridge.FetchSnippetImageCallback {
        private SnippetArticleViewHolder mViewHolder;
        private final SnippetArticle mSnippet;

        public FetchImageCallback(SnippetArticleViewHolder viewHolder, SnippetArticle snippet) {
            mViewHolder = viewHolder;
            mSnippet = snippet;
        }

        @Override
        public void onSnippetImageAvailable(Bitmap image) {
            if (mViewHolder == null) return;
            mViewHolder.fadeThumbnailIn(mSnippet, image);
        }

        public void cancel() {
            // TODO(treib): Pass the "cancel" on to the actual image fetcher.
            mViewHolder = null;
        }
    }

    private void cancelImageFetch() {
        if (mImageCallback != null) {
            mImageCallback.cancel();
            mImageCallback = null;
        }
    }

    private void fadeThumbnailIn(SnippetArticle snippet, Bitmap thumbnail) {
        mImageCallback = null;
        if (thumbnail == null) return; // Nothing to do, we keep the placeholder.

        // We need to crop and scale the downloaded bitmap, as the ImageView we set it on won't be
        // able to do so when using a TransitionDrawable (as opposed to the straight bitmap).
        // That's a limitation of TransitionDrawable, which doesn't handle layers of varying sizes.
        Resources res = mThumbnailView.getResources();
        int targetSize = res.getDimensionPixelSize(R.dimen.snippets_thumbnail_size);
        Bitmap scaledThumbnail = ThumbnailUtils.extractThumbnail(
                thumbnail, targetSize, targetSize, ThumbnailUtils.OPTIONS_RECYCLE_INPUT);

        // Store the bitmap to skip the download task next time we display this snippet.
        snippet.setThumbnailBitmap(scaledThumbnail);

        // Cross-fade between the placeholder and the thumbnail.
        Drawable[] layers = {mThumbnailView.getDrawable(),
                new BitmapDrawable(mThumbnailView.getResources(), scaledThumbnail)};
        TransitionDrawable transitionDrawable = new TransitionDrawable(layers);
        mThumbnailView.setImageDrawable(transitionDrawable);
        transitionDrawable.startTransition(FADE_IN_ANIMATION_TIME_MS);
    }

    private void updateFavicon(SnippetArticle snippet) {
        // The favicon size should match the textview height
        int widthSpec = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED);
        int heightSpec = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED);
        mPublisherTextView.measure(widthSpec, heightSpec);
        fetchFavicon(snippet, mPublisherTextView.getMeasuredHeight());
    }

    // Fetch the favicon using the article's url first. If that fails, try to fetch the
    // favicon for the hostname
    private void fetchFavicon(final SnippetArticle snippet, final int faviconSizePx) {
        mNewTabPageManager.getLocalFaviconImageForURL(
                snippet.mUrl, faviconSizePx, new FaviconImageCallback() {
                    @Override
                    public void onFaviconAvailable(Bitmap image, String iconUrl) {
                        if (image == null) {
                            fetchFaviconForHostname(snippet.mUrl, faviconSizePx);
                        } else {
                            setFaviconOnView(mPublisherTextView, faviconSizePx, image);
                        }
                    }
                });
    }

    private void fetchFaviconForHostname(String urlStr, final int sizePx) {
        URI uri = null;
        try {
            uri = new URI(urlStr);
        } catch (URISyntaxException e) {
            drawDefaultFavicon(sizePx);
            return;
        }

        mNewTabPageManager.getLocalFaviconImageForURL(
                String.format("%s://%s", uri.getScheme(), uri.getHost()), sizePx,
                new FaviconImageCallback() {
                    @Override
                    public void onFaviconAvailable(Bitmap image, String iconUrl) {
                        if (image == null) {
                            drawDefaultFavicon(sizePx);
                        } else {
                            setFaviconOnView(mPublisherTextView, sizePx, image);
                        }
                    }
                });
    }

    private void drawDefaultFavicon(int faviconSizePx) {
        Drawable newIcon = ApiCompatibilityUtils.getDrawable(
                mPublisherTextView.getContext().getResources(), R.drawable.default_favicon);
        setFaviconOnView(mPublisherTextView, faviconSizePx, newIcon);
    }

    private static void setFaviconOnView(TextView textview, int sizePx, Bitmap image) {
        Drawable domainFavicon = new BitmapDrawable(textview.getContext().getResources(), image);
        setFaviconOnView(textview, sizePx, domainFavicon);
    }

    private static void setFaviconOnView(TextView textview, int sizePx, Drawable drawable) {
        drawable.setBounds(0, 0, sizePx, sizePx);
        ApiCompatibilityUtils.setCompoundDrawablesRelative(textview, drawable, null, null, null);
        textview.setVisibility(View.VISIBLE);
    }

    @Override
    public boolean isDismissable() {
        return true;
    }
}
