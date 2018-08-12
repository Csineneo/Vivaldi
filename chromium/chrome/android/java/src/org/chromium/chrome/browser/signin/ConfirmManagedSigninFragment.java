// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import android.app.Activity;
import android.app.Dialog;
import android.app.DialogFragment;
import android.content.DialogInterface;
import android.content.DialogInterface.OnClickListener;
import android.os.Bundle;
import android.support.v7.app.AlertDialog;

import org.chromium.base.Log;
import org.chromium.chrome.R;

/**
 * This is a dialog asking the user to confirm that they want to sign in to a managed account.
 */
public class ConfirmManagedSigninFragment extends DialogFragment implements OnClickListener {
    private static final String TAG = "ConfirmManagedSignin";
    private static final String KEY_MANAGEMENT_DOMAIN = "managementDomain";

    // Tracks whether to abort signin in onDismiss.
    private boolean mAbortSignin = true;

    public static ConfirmManagedSigninFragment newInstance(String managementDomain) {
        ConfirmManagedSigninFragment dialogFragment = new ConfirmManagedSigninFragment();
        Bundle args = new Bundle();
        args.putString(KEY_MANAGEMENT_DOMAIN, managementDomain);
        dialogFragment.setArguments(args);
        return dialogFragment;
    }

    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        setCancelable(false);

        Activity activity = getActivity();
        String managementDomain = getArguments().getString(KEY_MANAGEMENT_DOMAIN);

        AlertDialog.Builder builder = new AlertDialog.Builder(activity, R.style.AlertDialogTheme);
        builder.setTitle(R.string.policy_dialog_title);
        builder.setMessage(activity.getResources().getString(
                R.string.policy_dialog_message, managementDomain));
        builder.setPositiveButton(R.string.policy_dialog_proceed, this);
        builder.setNegativeButton(R.string.policy_dialog_cancel, this);
        return builder.create();
    }

    @Override
    public void onClick(DialogInterface dialog, int which) {
        if (which == AlertDialog.BUTTON_POSITIVE) {
            Log.d(TAG, "Accepted policy management, proceeding with sign-in.");
            SigninManager.get(getActivity()).progressInteractiveSignInFlowManagedConfirmed();
            mAbortSignin = false;
        }
    }

    @Override
    public void onDismiss(DialogInterface dialogInterface) {
        super.onDismiss(dialogInterface);
        if (mAbortSignin) {
            Log.d(TAG, "Policy confirmation rejected; abort sign-in.");
            SigninManager.get(getActivity()).abortSignIn();
        }
    }
}
