// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.payments;

import android.app.Activity;
import android.graphics.Bitmap;
import android.os.Handler;
import android.text.TextUtils;

import org.chromium.base.Callback;
import org.chromium.base.Log;
import org.chromium.chrome.browser.autofill.PersonalDataManager;
import org.chromium.chrome.browser.autofill.PersonalDataManager.AutofillProfile;
import org.chromium.chrome.browser.favicon.FaviconHelper;
import org.chromium.chrome.browser.payments.ui.LineItem;
import org.chromium.chrome.browser.payments.ui.PaymentInformation;
import org.chromium.chrome.browser.payments.ui.PaymentOption;
import org.chromium.chrome.browser.payments.ui.PaymentRequestUI;
import org.chromium.chrome.browser.payments.ui.SectionInformation;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.util.UrlUtilities;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content_public.browser.WebContents;
import org.chromium.mojo.system.MojoException;
import org.chromium.mojom.payments.PaymentDetails;
import org.chromium.mojom.payments.PaymentItem;
import org.chromium.mojom.payments.PaymentOptions;
import org.chromium.mojom.payments.PaymentRequest;
import org.chromium.mojom.payments.PaymentRequestClient;
import org.chromium.mojom.payments.PaymentResponse;
import org.chromium.mojom.payments.ShippingOption;
import org.chromium.ui.base.WindowAndroid;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * Android implementation of the PaymentRequest service defined in
 * third_party/WebKit/public/platform/modules/payments/payment_request.mojom.
 */
public class PaymentRequestImpl implements PaymentRequest, PaymentRequestUI.Client,
        PaymentApp.InstrumentsCallback, PaymentInstrument.DetailsCallback {
    /**
     * The size for the favicon in density-independent pixels.
     */
    private static final int FAVICON_SIZE_DP = 24;

    private static final String TAG = "cr_PaymentRequest";
    private Activity mContext;
    private String mMerchantName;
    private String mOrigin;
    private Bitmap mFavicon;
    private List<PaymentApp> mApps;
    private PaymentRequestClient mClient;
    private Set<String> mSupportedMethods;
    private List<LineItem> mLineItems;
    private List<PaymentItem> mPaymentItems;
    private List<ShippingOption> mShippingOptions;
    private SectionInformation mShippingOptionsSection;
    private JSONObject mData;
    private SectionInformation mShippingAddressesSection;
    private List<PaymentApp> mPendingApps;
    private List<PaymentInstrument> mPendingInstruments;
    private SectionInformation mPaymentMethodsSection;
    private PaymentRequestUI mUI;
    private Callback<PaymentInformation> mPaymentInformationCallback;
    private Pattern mRegionCodePattern;
    private boolean mMerchantNeedsShippingAddress;

    /**
     * Builds the dialog.
     *
     * @param webContents The web contents that have invoked the PaymentRequest API.
     */
    public PaymentRequestImpl(WebContents webContents) {
        if (webContents == null) return;

        ContentViewCore contentViewCore = ContentViewCore.fromWebContents(webContents);
        if (contentViewCore == null) return;

        WindowAndroid window = contentViewCore.getWindowAndroid();
        if (window == null) return;

        mContext = window.getActivity().get();
        if (mContext == null) return;

        mMerchantName = webContents.getTitle();
        // The feature is available only in secure context, so it's OK to not show HTTPS.
        mOrigin = UrlUtilities.formatUrlForSecurityDisplay(webContents.getVisibleUrl(), false);

        final FaviconHelper faviconHelper = new FaviconHelper();
        float scale = mContext.getResources().getDisplayMetrics().density;
        faviconHelper.getLocalFaviconImageForURL(Profile.getLastUsedProfile(),
                webContents.getVisibleUrl(), (int) (FAVICON_SIZE_DP * scale + 0.5f),
                new FaviconHelper.FaviconImageCallback() {
                    @Override
                    public void onFaviconAvailable(Bitmap bitmap, String iconUrl) {
                        faviconHelper.destroy();
                        if (bitmap == null) return;
                        if (mUI == null) {
                            mFavicon = bitmap;
                            return;
                        }
                        mUI.setTitleBitmap(bitmap);
                    }
                });

        mApps = PaymentAppFactory.create(webContents);
        mRegionCodePattern = Pattern.compile(AutofillAddress.REGION_CODE_PATTERN);
    }

    /**
     * Called by the renderer to provide an endpoint for callbacks.
     */
    @Override
    public void setClient(PaymentRequestClient client) {
        assert mClient == null;

        mClient = client;

        if (mClient == null) return;

        if (mContext == null) {
            disconnectFromClientWithDebugMessage("Web contents don't have associated activity");
        }
    }

    /**
     * Called by the merchant website to show the payment request to the user.
     */
    @Override
    public void show(String[] supportedMethods, PaymentDetails details, PaymentOptions options,
            String stringifiedData) {
        if (mClient == null) return;

        if (mSupportedMethods != null) {
            disconnectFromClientWithDebugMessage("PaymentRequest.show() called more than once.");
            return;
        }

        mSupportedMethods = getValidatedSupportedMethods(supportedMethods);
        if (mSupportedMethods == null) {
            disconnectFromClientWithDebugMessage("Invalid payment methods");
            return;
        }

        if (!setLineItemsAndShippingOptionsOrDisconnectFromClient(details)) return;

        // If the merchant requests shipping and does not provide shipping options here, then the
        // merchant needs the shipping address to calculate shipping price and availability.
        boolean requestShipping = options != null && options.requestShipping;
        mMerchantNeedsShippingAddress = requestShipping && mShippingOptionsSection.isEmpty();

        mData = getValidatedData(mSupportedMethods, stringifiedData);
        if (mData == null) {
            disconnectFromClientWithDebugMessage("Invalid payment method specific data");
            return;
        }

        List<AutofillAddress> addresses = new ArrayList<>();
        List<AutofillProfile> profiles = PersonalDataManager.getInstance().getAddressOnlyProfiles();
        for (int i = 0; i < profiles.size(); i++) {
            AutofillProfile profile = profiles.get(i);
            if (profile.getCountryCode() != null
                    && mRegionCodePattern.matcher(profile.getCountryCode()).matches()
                    && profile.getStreetAddress() != null && profile.getRegion() != null
                    && profile.getLocality() != null && profile.getDependentLocality() != null
                    && profile.getPostalCode() != null && profile.getSortingCode() != null
                    && profile.getCompanyName() != null && profile.getFullName() != null) {
                addresses.add(new AutofillAddress(profile));
            }
        }

        if (addresses.isEmpty()) {
            mShippingAddressesSection = new SectionInformation();
        } else if (mShippingOptionsSection.getSelectedItem() != null) {
            mShippingAddressesSection = new SectionInformation(0, addresses);
        } else {
            mShippingAddressesSection =
                    new SectionInformation(SectionInformation.NO_SELECTION, addresses);
        }

        mPendingApps = new ArrayList<>(mApps);
        mPendingInstruments = new ArrayList<>();
        boolean isGettingInstruments = false;

        for (int i = 0; i < mApps.size(); i++) {
            PaymentApp app = mApps.get(i);
            Set<String> appMethods = app.getSupportedMethodNames();
            appMethods.retainAll(mSupportedMethods);
            if (appMethods.isEmpty()) {
                mPendingApps.remove(app);
            } else {
                isGettingInstruments = true;
                app.getInstruments(mPaymentItems, this);
            }
        }

        if (!isGettingInstruments) mPaymentMethodsSection = new SectionInformation();

        mUI = PaymentRequestUI.show(mContext, this, requestShipping, mMerchantName, mOrigin);
        if (mFavicon != null) mUI.setTitleBitmap(mFavicon);
        mFavicon = null;
    }

    /**
     * Called by merchant to update the shipping options and line items after the user has selected
     * their shipping address or shipping option.
     */
    @Override
    public void updateWith(PaymentDetails details) {
        if (mClient == null) return;

        if (mUI == null) {
            disconnectFromClientWithDebugMessage(
                    "PaymentRequestUpdateEvent.updateWith() called without PaymentRequest.show()");
            return;
        }

        if (!setLineItemsAndShippingOptionsOrDisconnectFromClient(details)) return;

        // Empty shipping options means the merchant cannot ship to the user's selected shipping
        // address.
        if (mShippingOptionsSection.isEmpty() && !mMerchantNeedsShippingAddress) {
            disconnectFromClientWithDebugMessage("Merchant indicates inablity to ship although "
                    + "originally indicated that can ship anywhere");
        }

        mUI.updateOrderSummarySection(mLineItems);
        mUI.updateShippingOptionsSection(mShippingOptionsSection);
    }

    private boolean setLineItemsAndShippingOptionsOrDisconnectFromClient(PaymentDetails details) {
        mLineItems = getValidatedLineItems(details);
        if (mLineItems == null) {
            disconnectFromClientWithDebugMessage("Invalid line items");
            return false;
        }
        mPaymentItems = Arrays.asList(details.items);

        mShippingOptionsSection =
                getValidatedShippingOptions(details.items[0].amount.currencyCode, details);
        if (mShippingOptionsSection == null) {
            disconnectFromClientWithDebugMessage("Invalid shipping options");
            return false;
        }
        mShippingOptions = Arrays.asList(details.shippingOptions);

        return true;
    }

    private HashSet<String> getValidatedSupportedMethods(String[] methods) {
        // Payment methods are required.
        if (methods == null || methods.length == 0) return null;

        HashSet<String> result = new HashSet<>();
        for (int i = 0; i < methods.length; i++) {
            // Payment methods should be non-empty.
            if (TextUtils.isEmpty(methods[i])) return null;
            result.add(methods[i]);
        }

        return result;
    }

    private List<LineItem> getValidatedLineItems(PaymentDetails details) {
        // Line items are required.
        if (details == null || details.items == null || details.items.length == 0) return null;

        for (int i = 0; i < details.items.length; i++) {
            PaymentItem item = details.items[i];
            // "id", "label", "currencyCode", and "value" should be non-empty.
            if (item == null || TextUtils.isEmpty(item.id) || TextUtils.isEmpty(item.label)
                    || item.amount == null || TextUtils.isEmpty(item.amount.currencyCode)
                    || TextUtils.isEmpty(item.amount.value)) {
                return null;
            }
        }

        CurrencyStringFormatter formatter = new CurrencyStringFormatter(
                details.items[0].amount.currencyCode, Locale.getDefault());

        // Currency codes should be in correct format.
        if (!formatter.isValidAmountCurrencyCode(details.items[0].amount.currencyCode)) return null;

        List<LineItem> result = new ArrayList<>(details.items.length);
        for (int i = 0; i < details.items.length; i++) {
            PaymentItem item = details.items[i];

            // All currency codes must match.
            if (!item.amount.currencyCode.equals(details.items[0].amount.currencyCode)) return null;

            // Value should be in correct format.
            if (!formatter.isValidAmountValue(item.amount.value)) return null;

            result.add(new LineItem(item.label,
                    i == details.items.length - 1 ? item.amount.currencyCode : "",
                    formatter.format(item.amount.value)));
        }

        return result;
    }

    private SectionInformation getValidatedShippingOptions(
            String itemsCurrencyCode, PaymentDetails details) {
        // Shipping options are optional.
        if (details.shippingOptions == null || details.shippingOptions.length == 0) {
            return new SectionInformation();
        }

        CurrencyStringFormatter formatter =
                new CurrencyStringFormatter(itemsCurrencyCode, Locale.getDefault());

        for (int i = 0; i < details.shippingOptions.length; i++) {
            ShippingOption option = details.shippingOptions[i];

            // Each "id", "label", "currencyCode", and "value" should be non-empty.
            // Each "value" should be a valid amount value.
            // Each "currencyCode" should match the line items' currency codes.
            if (option == null || TextUtils.isEmpty(option.id) || TextUtils.isEmpty(option.label)
                    || option.amount == null || TextUtils.isEmpty(option.amount.currencyCode)
                    || TextUtils.isEmpty(option.amount.value)
                    || !itemsCurrencyCode.equals(option.amount.currencyCode)
                    || !formatter.isValidAmountValue(option.amount.value)) {
                return null;
            }
        }

        boolean isSameAsCurrentOptions = true;
        if (mShippingOptions == null || mShippingOptions.size() != details.shippingOptions.length) {
            isSameAsCurrentOptions = false;
        } else {
            for (int i = 0; i < details.shippingOptions.length; i++) {
                ShippingOption newOption = details.shippingOptions[i];
                ShippingOption currentOption = mShippingOptions.get(i);
                if (!newOption.id.equals(currentOption.id)
                        || !newOption.label.equals(currentOption.label)
                        || !newOption.amount.currencyCode.equals(currentOption.amount.currencyCode)
                        || !newOption.amount.value.equals(currentOption.amount.value)) {
                    isSameAsCurrentOptions = false;
                    break;
                }
            }
        }
        if (isSameAsCurrentOptions) return mShippingOptionsSection;

        List<PaymentOption> result = new ArrayList<>();
        for (int i = 0; i < details.shippingOptions.length; i++) {
            ShippingOption option = details.shippingOptions[i];
            result.add(new PaymentOption(option.id, option.label,
                    formatter.format(option.amount.value), PaymentOption.NO_ICON));
        }

        return new SectionInformation(result.size() == 1 ? 0 : SectionInformation.NO_SELECTION,
                result);
    }

    private JSONObject getValidatedData(Set<String> supportedMethods, String stringifiedData) {
        if (TextUtils.isEmpty(stringifiedData)) return new JSONObject();

        JSONObject result;
        try {
            result = new JSONObject(stringifiedData);
        } catch (JSONException e) {
            // Payment method specific data should be a JSON object.
            return null;
        }

        Iterator<String> it = result.keys();
        while (it.hasNext()) {
            String name = it.next();
            // Each key should be one of the supported payment methods.
            if (!supportedMethods.contains(name)) return null;
            // Each value should be a JSON object.
            if (result.optJSONObject(name) == null) return null;
        }

        return result;
    }

    /**
     * Called to retrieve the data to show in the initial PaymentRequest UI.
     */
    @Override
    public void getDefaultPaymentInformation(Callback<PaymentInformation> callback) {
        mPaymentInformationCallback = callback;

        if (mPaymentMethodsSection == null) return;

        new Handler().post(new Runnable() {
            @Override
            public void run() {
                provideDefaultPaymentInformation();
            }
        });
    }

    private void provideDefaultPaymentInformation() {
        mPaymentInformationCallback.onResult(new PaymentInformation(
                mLineItems.get(mLineItems.size() - 1), mShippingAddressesSection.getSelectedItem(),
                mShippingOptionsSection.getSelectedItem(),
                mPaymentMethodsSection.getSelectedItem()));
        mPaymentInformationCallback = null;
    }

    @Override
    public void getLineItems(final Callback<List<LineItem>> callback) {
        new Handler().post(new Runnable() {
            @Override
            public void run() {
                callback.onResult(mLineItems);
            }
        });
    }

    @Override
    public void getShippingAddresses(final Callback<SectionInformation> callback) {
        new Handler().post(new Runnable() {
            @Override
            public void run() {
                callback.onResult(mShippingAddressesSection);
            }
        });
    }

    @Override
    public void getShippingOptions(final Callback<SectionInformation> callback) {
        new Handler().post(new Runnable() {
            @Override
            public void run() {
                callback.onResult(mShippingOptionsSection);
            }
        });
    }

    @Override
    public void getPaymentMethods(final Callback<SectionInformation> callback) {
        assert mPaymentMethodsSection != null;
        new Handler().post(new Runnable() {
            @Override
            public void run() {
                callback.onResult(mPaymentMethodsSection);
            }
        });
    }

    @Override
    public void onShippingAddressChanged(PaymentOption selectedShippingAddress) {
        assert selectedShippingAddress instanceof AutofillAddress;
        mShippingAddressesSection.setSelectedItem(selectedShippingAddress);
        if (mMerchantNeedsShippingAddress) {
            mClient.onShippingAddressChange(
                    ((AutofillAddress) selectedShippingAddress).toShippingAddress());
        }
    }

    @Override
    public void onShippingOptionChanged(PaymentOption selectedShippingOption) {
        mShippingOptionsSection.setSelectedItem(selectedShippingOption);
        mClient.onShippingOptionChange(selectedShippingOption.getIdentifier());
    }

    @Override
    public void onPaymentMethodChanged(PaymentOption selectedPaymentMethod) {
        assert selectedPaymentMethod instanceof PaymentInstrument;
        mPaymentMethodsSection.setSelectedItem(selectedPaymentMethod);
    }

    @Override
    public void onPayClicked(PaymentOption selectedShippingAddress,
            PaymentOption selectedShippingOption, PaymentOption selectedPaymentMethod) {
        assert selectedPaymentMethod instanceof PaymentInstrument;
        PaymentInstrument instrument = (PaymentInstrument) selectedPaymentMethod;
        instrument.getDetails(mMerchantName, mOrigin, mPaymentItems,
                mData.optJSONObject(instrument.getMethodName()), this);
    }

    @Override
    public void onDismiss() {
        disconnectFromClientWithDebugMessage("Dialog dismissed");
        closeUI(false);
    }

    /**
     * Called by the merchant website to abort the payment.
     */
    @Override
    public void abort() {
        closeClient();
        closeUI(false);
    }

    /**
     * Called when the merchant website has processed the payment.
     */
    @Override
    public void complete(boolean success) {
        closeUI(success);
    }

    /**
     * Called when the renderer closes the Mojo connection.
     */
    @Override
    public void close() {
        closeClient();
        closeUI(false);
    }

    /**
     * Called when the Mojo connection encounters an error.
     */
    @Override
    public void onConnectionError(MojoException e) {
        closeClient();
        closeUI(false);
    }

    /**
     * Called after retrieving the list of payment instruments in an app.
     */
    @Override
    public void onInstrumentsReady(PaymentApp app, List<PaymentInstrument> instruments) {
        mPendingApps.remove(app);

        if (instruments != null) {
            for (int i = 0; i < instruments.size(); i++) {
                PaymentInstrument instrument = instruments.get(i);
                if (mSupportedMethods.contains(instrument.getMethodName())) {
                    mPendingInstruments.add(instrument);
                } else {
                    instrument.dismiss();
                }
            }
        }

        if (mPendingApps.isEmpty()) {
            if (mPendingInstruments.isEmpty()) {
                mPaymentMethodsSection = new SectionInformation();
            } else {
                mPaymentMethodsSection = new SectionInformation(0, mPendingInstruments);
                mPendingInstruments.clear();
            }

            if (mPaymentInformationCallback != null) provideDefaultPaymentInformation();
        }
    }

    /**
     * Called after retrieving instrument details.
     */
    @Override
    public void onInstrumentDetailsReady(String methodName, String stringifiedDetails) {
        PaymentResponse response = new PaymentResponse();
        response.methodName = methodName;
        response.stringifiedDetails = stringifiedDetails;

        PaymentOption selectedShippingAddress = mShippingAddressesSection.getSelectedItem();
        if (selectedShippingAddress != null) {
            // Shipping addresses are created in show(). The should all be instances of
            // AutofillAddress.
            assert selectedShippingAddress instanceof AutofillAddress;
            response.shippingAddress =
                    ((AutofillAddress) selectedShippingAddress).toShippingAddress();
        }

        PaymentOption selectedShippingOption = mShippingOptionsSection.getSelectedItem();
        if (selectedShippingOption != null && selectedShippingOption.getIdentifier() != null) {
            response.shippingOptionId = selectedShippingOption.getIdentifier();
        }

        mClient.onPaymentResponse(response);
    }

    /**
     * Called if unable to retrieve instrument details.
     */
    @Override
    public void onInstrumentDetailsError() {
        disconnectFromClientWithDebugMessage("Failed to retrieve payment instrument details");
        closeUI(false);
    }

    private void disconnectFromClientWithDebugMessage(String debugMessage) {
        Log.d(TAG, debugMessage);
        mClient.onError();
        closeClient();
    }

    /**
     * Closes the UI. If the client is still connected, then it's notified of UI hiding.
     */
    private void closeUI(boolean paymentSuccess) {
        if (mUI != null) {
            mUI.close(paymentSuccess, new Runnable() {
                @Override
                public void run() {
                    if (mClient != null) mClient.onComplete();
                    closeClient();
                }
            });
            mUI = null;
        }

        if (mPaymentMethodsSection != null) {
            for (int i = 0; i < mPaymentMethodsSection.getSize(); i++) {
                PaymentOption option = mPaymentMethodsSection.getItem(i);
                assert option instanceof PaymentInstrument;
                ((PaymentInstrument) option).dismiss();
            }
            mPaymentMethodsSection = null;
        }
    }

    private void closeClient() {
        if (mClient != null) mClient.close();
        mClient = null;
    }
}
