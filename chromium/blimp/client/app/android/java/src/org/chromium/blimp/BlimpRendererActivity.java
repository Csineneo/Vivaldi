// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.blimp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

import org.chromium.base.Log;
import org.chromium.base.library_loader.ProcessInitException;
import org.chromium.blimp.auth.RetryingTokenSource;
import org.chromium.blimp.auth.TokenSource;
import org.chromium.blimp.auth.TokenSourceImpl;
import org.chromium.blimp.input.TextInputFeature;
import org.chromium.blimp.session.BlimpClientSession;
import org.chromium.blimp.session.TabControlFeature;
import org.chromium.blimp.toolbar.Toolbar;
import org.chromium.ui.widget.Toast;

/**
 * The {@link Activity} for rendering the main Blimp client.  This loads the Blimp rendering stack
 * and displays it.
 */
public class BlimpRendererActivity extends Activity
        implements BlimpLibraryLoader.Callback, TokenSource.Callback, BlimpClientSession.Callback {
    private static final int ACCOUNT_CHOOSER_INTENT_REQUEST_CODE = 100;
    private static final String TAG = "BlimpRendererActivity";

    /** Provides user authentication tokens that can be used to query for engine assignments.  This
     *  can potentially query GoogleAuthUtil for an OAuth2 authentication token with userinfo.email
     *  privileges for a chosen Android account. */
    private TokenSource mTokenSource;

    private BlimpView mBlimpView;
    private Toolbar mToolbar;
    private BlimpClientSession mBlimpClientSession;
    private TabControlFeature mTabControlFeature;
    private TextInputFeature mTextInputFeature;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Build a TokenSource that will internally retry accessing the underlying TokenSourceImpl.
        // This will exponentially backoff while it tries to get the access token.  See
        // {@link RetryingTokenSource} for more information.  The underlying
        // TokenSourceImpl will attempt to query GoogleAuthUtil, but might fail if there is no
        // account selected, in which case it will ask this Activity to show an account chooser and
        // notify it of the selection result.
        mTokenSource = new RetryingTokenSource(new TokenSourceImpl(this));
        mTokenSource.setCallback(this);
        mTokenSource.getToken();

        try {
            BlimpLibraryLoader.startAsync(this, this);
        } catch (ProcessInitException e) {
            Log.e(TAG, "Native startup exception", e);
            System.exit(-1);
            return;
        }
    }

    @Override
    protected void onDestroy() {
        if (mTabControlFeature != null) {
            mTabControlFeature.destroy();
            mTabControlFeature = null;
        }

        if (mBlimpView != null) {
            mBlimpView.destroyRenderer();
            mBlimpView = null;
        }

        if (mToolbar != null) {
            mToolbar.destroy();
            mToolbar = null;
        }

        if (mTokenSource != null) {
            mTokenSource.destroy();
            mTokenSource = null;
        }

        // Destroy the BlimpClientSession last, as all other features may rely on it.
        if (mBlimpClientSession != null) {
            mBlimpClientSession.destroy();
            mBlimpClientSession = null;
        }

        super.onDestroy();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        switch (requestCode) {
            case ACCOUNT_CHOOSER_INTENT_REQUEST_CODE:
                if (resultCode == RESULT_OK) {
                    mTokenSource.onAccountSelected(data);
                    mTokenSource.getToken();
                } else {
                    onTokenUnavailable(false);
                }
                break;
        }
    }

    @Override
    public void onBackPressed() {
        // Check if the toolbar can handle the back navigation.
        if (mToolbar != null && mToolbar.onBackPressed()) return;

        // If not, use the default Activity behavior.
        super.onBackPressed();
    }

    // BlimpLibraryLoader.Callback implementation.
    @Override
    public void onStartupComplete(boolean success) {
        if (!success) {
            Log.e(TAG, "Native startup failed");
            finish();
            return;
        }

        setContentView(R.layout.blimp_main);

        mBlimpClientSession = new BlimpClientSession(this);

        mBlimpView = (BlimpView) findViewById(R.id.renderer);
        mBlimpView.initializeRenderer(mBlimpClientSession);

        mToolbar = (Toolbar) findViewById(R.id.toolbar);
        mToolbar.initialize(mBlimpClientSession);

        mTabControlFeature = new TabControlFeature(mBlimpClientSession, mBlimpView);
        mToolbar.loadUrl("http://www.google.com/");

        mTextInputFeature = (TextInputFeature) findViewById(R.id.editText);
    }

    // TokenSource.Callback implementation.
    @Override
    public void onTokenReceived(String token) {
        if (mBlimpClientSession != null) mBlimpClientSession.connect(token);
    }

    @Override
    public void onTokenUnavailable(boolean isTransient) {
        // Ignore isTransient here because we're relying on the auto-retry TokenSource.
        // TODO(dtrainor): Show a better error dialog/message.
        Toast.makeText(this, R.string.signin_get_token_failed, Toast.LENGTH_LONG).show();
    }

    @Override
    public void onNeedsAccountToBeSelected(Intent suggestedIntent) {
        startActivityForResult(suggestedIntent, ACCOUNT_CHOOSER_INTENT_REQUEST_CODE);
    }

    // BlimpClientSession.Callback implementation.
    @Override
    public void onAssignmentReceived(int result, int suggestedMessageResourceId) {
        Toast.makeText(this, suggestedMessageResourceId, Toast.LENGTH_LONG).show();
    }
}
