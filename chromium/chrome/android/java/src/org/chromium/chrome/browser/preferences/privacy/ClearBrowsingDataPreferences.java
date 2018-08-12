// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.preferences.privacy;

import android.app.Activity;
import android.app.ProgressDialog;
import android.os.Bundle;
import android.preference.Preference;
import android.preference.PreferenceFragment;
import android.widget.ListView;

import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.BrowsingDataType;
import org.chromium.chrome.browser.preferences.ButtonPreference;
import org.chromium.chrome.browser.preferences.ClearBrowsingDataCheckBoxPreference;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.chrome.browser.preferences.SpinnerPreference;
import org.chromium.chrome.browser.preferences.privacy.BrowsingDataCounterBridge.BrowsingDataCounterCallback;
import org.chromium.sync.signin.ChromeSigninController;

import java.util.Arrays;
import java.util.EnumSet;

/**
 * Modal dialog with options for selection the type of browsing data
 * to clear (history, cookies), triggered from a preference.
 */
public class ClearBrowsingDataPreferences extends PreferenceFragment
        implements PrefServiceBridge.OnClearBrowsingDataListener,
                   Preference.OnPreferenceClickListener,
                   Preference.OnPreferenceChangeListener{
    /**
     * Represents a single item in the dialog.
     */
    private static class Item implements BrowsingDataCounterCallback,
                                         Preference.OnPreferenceClickListener {
        private final ClearBrowsingDataPreferences mParent;
        private final DialogOption mOption;
        private final ClearBrowsingDataCheckBoxPreference mCheckbox;
        private BrowsingDataCounterBridge mCounter;
        private boolean mShouldAnnounceCounterResult;

        public Item(ClearBrowsingDataPreferences parent,
                    DialogOption option,
                    ClearBrowsingDataCheckBoxPreference checkbox,
                    boolean selected,
                    boolean enabled) {
            super();
            mParent = parent;
            mOption = option;
            mCheckbox = checkbox;
            mCounter = new BrowsingDataCounterBridge(this, mOption.getDataType());

            mCheckbox.setOnPreferenceClickListener(this);
            mCheckbox.setEnabled(enabled);
            mCheckbox.setChecked(selected);
            mCheckbox.setSummaryOff("");  // No summary when unchecked.
        }

        public void destroy() {
            mCounter.destroy();
        }

        public DialogOption getOption() {
            return mOption;
        }

        public boolean isSelected() {
            return mCheckbox.isChecked();
        }

        @Override
        public boolean onPreferenceClick(Preference preference) {
            assert mCheckbox == preference;

            mParent.updateButtonState();
            mShouldAnnounceCounterResult = true;
            PrefServiceBridge.getInstance().setBrowsingDataDeletionPreference(
                    mOption.getDataType(), mCheckbox.isChecked());
            return true;
        }

        @Override
        public void onCounterFinished(String result) {
            mCheckbox.setSummaryOn(result);
            if (mShouldAnnounceCounterResult) {
                mCheckbox.announceForAccessibility(result);
            }
        }

        /**
         * Sets whether the BrowsingDataCounter result should be announced. This is when the counter
         * recalculation was caused by a checkbox state change (as opposed to fragment
         * initialization or time period change).
         */
        public void setShouldAnnounceCounterResult(boolean value) {
            mShouldAnnounceCounterResult = value;
        }
    }

    private static final String PREF_HISTORY = "clear_history_checkbox";
    private static final String PREF_COOKIES = "clear_cookies_checkbox";
    private static final String PREF_CACHE = "clear_cache_checkbox";
    private static final String PREF_PASSWORDS = "clear_passwords_checkbox";
    private static final String PREF_FORM_DATA = "clear_form_data_checkbox";
    private static final String PREF_BOOKMARKS = "clear_bookmarks_checkbox";

    private static final String PREF_SUMMARY = "summary";
    private static final String PREF_TIME_RANGE = "time_period_spinner";

    /** The "Clear" button preference. Referenced in tests. */
    public static final String PREF_CLEAR_BUTTON = "clear_button";

    /** The tag used for logging. */
    public static final String TAG = "ClearBrowsingDataPreferences";

    /**
     * Enum for Dialog options to be displayed in the dialog.
     */
    public enum DialogOption {
        CLEAR_HISTORY(BrowsingDataType.HISTORY, PREF_HISTORY),
        CLEAR_COOKIES_AND_SITE_DATA(BrowsingDataType.COOKIES, PREF_COOKIES),
        CLEAR_CACHE(BrowsingDataType.CACHE, PREF_CACHE),
        CLEAR_PASSWORDS(BrowsingDataType.PASSWORDS, PREF_PASSWORDS),
        CLEAR_FORM_DATA(BrowsingDataType.FORM_DATA, PREF_FORM_DATA),
        // Clear bookmarks is only used by ClearSyncData dialog.
        CLEAR_BOOKMARKS_DATA(BrowsingDataType.BOOKMARKS, PREF_BOOKMARKS);

        private final int mDataType;
        private final String mPreferenceKey;

        private DialogOption(int dataType, String preferenceKey) {
            mDataType = dataType;
            mPreferenceKey = preferenceKey;
        }

        public int getDataType() {
            return mDataType;
        }

        /**
         * @return String The key of the corresponding preference.
         */
        public String getPreferenceKey() {
            return mPreferenceKey;
        }
    }

    /**
     * An option to be shown in the time period spiner.
     */
    private static class TimePeriodSpinnerOption {
        private int mTimePeriod;
        private String mTitle;

        /**
         * Constructs this time period spinner option.
         * @param timePeriod The time period represented as an int from the shared enum
         *     {@link org.chromium.chrome.browser.TimePeriod}.
         * @param title The text that will be used to represent this item in the spinner.
         */
        public TimePeriodSpinnerOption(int timePeriod, String title) {
            mTimePeriod = timePeriod;
            mTitle = title;
        }

        /**
         * @return The time period represented as an int from the shared enum
         *     {@link org.chromium.chrome.browser.TimePeriod}
         */
        public int getTimePeriod() {
            return mTimePeriod;
        }

        @Override
        public String toString() {
            return mTitle;
        }
    }

    private ProgressDialog mProgressDialog;
    private boolean mCanDeleteBrowsingHistory;
    private Item[] mItems;

    protected final EnumSet<DialogOption> getSelectedOptions() {
        EnumSet<DialogOption> selected = EnumSet.noneOf(DialogOption.class);
        for (Item item : mItems) {
            if (item.isSelected()) selected.add(item.getOption());
        }
        return selected;
    }

    /**
     * Requests the browsing data corresponding to the given dialog options to be deleted.
     * @param options The dialog options whose corresponding data should be deleted.
     */
    protected final void clearBrowsingData(EnumSet<DialogOption> options) {
        int[] dataTypes = new int[options.size()];

        int i = 0;
        for (DialogOption option : options) {
            dataTypes[i] = option.getDataType();
            ++i;
        }

        Object spinnerSelection =
                ((SpinnerPreference) findPreference(PREF_TIME_RANGE)).getSelectedOption();
        int timePeriod = ((TimePeriodSpinnerOption) spinnerSelection).getTimePeriod();
        PrefServiceBridge.getInstance().clearBrowsingData(this, dataTypes, timePeriod);
    }

    protected void dismissProgressDialog() {
        android.util.Log.i(TAG, "in dismissProgressDialog");
        if (mProgressDialog != null && mProgressDialog.isShowing()) {
            android.util.Log.i(TAG, "progress dialog dismissed");
            mProgressDialog.dismiss();
        }
        mProgressDialog = null;
    }

    /**
     * Returns the Array of dialog options. Options are displayed in the same
     * order as they appear in the array.
     */
    protected DialogOption[] getDialogOptions() {
        return new DialogOption[] {
            DialogOption.CLEAR_HISTORY,
            DialogOption.CLEAR_COOKIES_AND_SITE_DATA,
            DialogOption.CLEAR_CACHE,
            DialogOption.CLEAR_PASSWORDS,
            DialogOption.CLEAR_FORM_DATA};
    }

    /**
     * Returns the Array of time periods. Options are displayed in the same order as they appear
     * in the array.
     */
    private TimePeriodSpinnerOption[] getTimePeriodSpinnerOptions() {
        Activity activity = getActivity();

        TimePeriodSpinnerOption[] options = new TimePeriodSpinnerOption[] {
                new TimePeriodSpinnerOption(org.chromium.chrome.browser.TimePeriod.LAST_HOUR,
                        activity.getString(R.string.clear_browsing_data_period_hour)),
                new TimePeriodSpinnerOption(org.chromium.chrome.browser.TimePeriod.LAST_DAY,
                        activity.getString(R.string.clear_browsing_data_period_day)),
                new TimePeriodSpinnerOption(org.chromium.chrome.browser.TimePeriod.LAST_WEEK,
                        activity.getString(R.string.clear_browsing_data_period_week)),
                new TimePeriodSpinnerOption(org.chromium.chrome.browser.TimePeriod.FOUR_WEEKS,
                        activity.getString(R.string.clear_browsing_data_period_four_weeks)),
                new TimePeriodSpinnerOption(org.chromium.chrome.browser.TimePeriod.EVERYTHING,
                        activity.getString(R.string.clear_browsing_data_period_everything))};

        return options;
    }

    /**
     * Decides whether a given dialog option should be selected when the dialog is initialized.
     * @param option The option in question.
     * @return boolean Whether the given option should be preselected.
     */
    protected boolean isOptionSelectedByDefault(DialogOption option) {
        return PrefServiceBridge.getInstance().getBrowsingDataDeletionPreference(
            option.getDataType());
    }

    // Called when "clear browsing data" completes.
    // Implements the ChromePreferences.OnClearBrowsingDataListener interface.
    @Override
    public void onBrowsingDataCleared() {
        if (getActivity() == null) return;

        dismissProgressDialog();
        getActivity().finish();
    }

    @Override
    public boolean onPreferenceClick(Preference preference) {
        if (preference.getKey().equals(PREF_CLEAR_BUTTON)) {
            dismissProgressDialog();
            onOptionSelected();
            return true;
        }
        return false;
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object value) {
        if (preference.getKey().equals(PREF_TIME_RANGE)) {
            // Inform the items that a recalculation is going to happen as a result of the time
            // period change.
            for (Item item : mItems) {
                item.setShouldAnnounceCounterResult(false);
            }

            PrefServiceBridge.getInstance().setBrowsingDataDeletionTimePeriod(
                    ((TimePeriodSpinnerOption) value).getTimePeriod());
            return true;
        }
        return false;
    }

    /**
     * Disable the "Clear" button if none of the options are selected. Otherwise, enable it.
     */
    private void updateButtonState() {
        ButtonPreference clearButton = (ButtonPreference) findPreference(PREF_CLEAR_BUTTON);
        if (clearButton == null) return;
        boolean isEnabled = !getSelectedOptions().isEmpty();
        clearButton.setEnabled(isEnabled);
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getActivity().setTitle(R.string.clear_browsing_data_title);
        addPreferencesFromResource(R.xml.clear_browsing_data_preferences);
        DialogOption[] options = getDialogOptions();
        mItems = new Item[options.length];
        for (int i = 0; i < options.length; i++) {
            boolean enabled = true;

            // It is possible to disable the deletion of browsing history.
            if (options[i] == DialogOption.CLEAR_HISTORY
                    && !PrefServiceBridge.getInstance().canDeleteBrowsingHistory()) {
                enabled = false;
                PrefServiceBridge.getInstance().setBrowsingDataDeletionPreference(
                        DialogOption.CLEAR_HISTORY.getDataType(), false);
            }

            mItems[i] = new Item(
                this,
                options[i],
                (ClearBrowsingDataCheckBoxPreference) findPreference(options[i].getPreferenceKey()),
                isOptionSelectedByDefault(options[i]),
                enabled);
        }

        // Not all checkboxes defined in the layout are necessarily handled by this class
        // or a particular subclass. Hide those that are not.
        EnumSet<DialogOption> unboundOptions = EnumSet.allOf(DialogOption.class);
        unboundOptions.removeAll(Arrays.asList(getDialogOptions()));
        for (DialogOption option : unboundOptions) {
            getPreferenceScreen().removePreference(findPreference(option.getPreferenceKey()));
        }

        // The time range selection spinner.
        SpinnerPreference spinner = (SpinnerPreference) findPreference(PREF_TIME_RANGE);
        spinner.setOnPreferenceChangeListener(this);
        TimePeriodSpinnerOption[] spinnerOptions = getTimePeriodSpinnerOptions();
        int selectedTimePeriod =
                PrefServiceBridge.getInstance().getBrowsingDataDeletionTimePeriod();
        int spinnerOptionIndex = -1;
        for (int i = 0; i < spinnerOptions.length; ++i) {
            if (spinnerOptions[i].getTimePeriod() == selectedTimePeriod) {
                spinnerOptionIndex = i;
                break;
            }
        }
        assert spinnerOptionIndex != -1;
        spinner.setOptions(spinnerOptions, spinnerOptionIndex);

        // The "Clear" button.
        ButtonPreference clearButton = (ButtonPreference) findPreference(PREF_CLEAR_BUTTON);
        clearButton.setOnPreferenceClickListener(this);
        clearButton.setShouldDisableView(true);

        // Only one footnote should be shown, the unsynced or synced version, depending on whether
        // the user is signed in.
        Preference summary = findPreference(PREF_SUMMARY);
        if (ChromeSigninController.get(getActivity()).isSignedIn()) {
            summary.setSummary(R.string.clear_browsing_data_footnote_signed);
        } else {
            summary.setSummary(R.string.clear_browsing_data_footnote);
        }
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
        // Now that the dialog's view has been created, update the button state.
        updateButtonState();

        // Remove the dividers between checkboxes.
        ((ListView) getView().findViewById(android.R.id.list)).setDivider(null);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        dismissProgressDialog();
        for (Item item : mItems) {
            item.destroy();
        }
    }

    /**
     * Called when PositiveButton is clicked for the dialog.
     */
    protected void onOptionSelected() {
        showProgressDialog();
        clearBrowsingData(getSelectedOptions());
    }

    protected final void showProgressDialog() {
        if (getActivity() == null) return;

        android.util.Log.i(TAG, "progress dialog shown");
        mProgressDialog = ProgressDialog.show(getActivity(),
                getActivity().getString(R.string.clear_browsing_data_progress_title),
                getActivity().getString(R.string.clear_browsing_data_progress_message), true,
                false);
    }

    @VisibleForTesting
    ProgressDialog getProgressDialog() {
        return mProgressDialog;
    }
}
