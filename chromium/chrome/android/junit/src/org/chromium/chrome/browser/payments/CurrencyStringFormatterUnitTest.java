// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.payments;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameters;

import java.util.Arrays;
import java.util.Collection;
import java.util.Locale;

/**
 * Unit tests for the CurrencyStringFormatter class.
 */
@RunWith(Parameterized.class)
public class CurrencyStringFormatterUnitTest {
    /**
     * Unicode non-breaking space.
     */
    private static final String SPACE = "\u00A0";

    private enum ExpectedValidity {
        VALID_AMOUNT,
        INVALID_AMOUNT_CURRENCY_CODE,
        INVALID_AMOUNT_VALUE,
    };

    @Parameters
    public static Collection<Object[]> data() {
        return Arrays.asList(new Object[][] {
            {"55.00", "USD", "en-US", "USD", "$55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "USD", "en-CA", "USD", "$55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "USD", "fr-CA", "USD", "$55,00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "USD", "fr-FR", "USD", "$55,00", ExpectedValidity.VALID_AMOUNT},

            {"55.00", "EUR", "en-US", "EUR", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "EUR", "en-CA", "EUR", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "EUR", "fr-CA", "EUR", "55,00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "EUR", "fr-FR", "EUR", "55,00", ExpectedValidity.VALID_AMOUNT},

            {"55.00", "CAD", "en-US", "CAD", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "CAD", "en-CA", "CAD", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "CAD", "fr-CA", "CAD", "55,00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "CAD", "fr-FR", "CAD", "55,00", ExpectedValidity.VALID_AMOUNT},

            {"55.12", "JPY", "ja-JP", "JPY", "55.12", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "JPY", "ja-JP", "JPY", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.0", "JPY", "ja-JP", "JPY", "55.0", ExpectedValidity.VALID_AMOUNT},
            {"55", "JPY", "ja-JP", "JPY", "55", ExpectedValidity.VALID_AMOUNT},

            // Unofficial ISO 4217 currency code.
            {"55.00", "BTX", "en-US", "BTX", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"-55.00", "BTX", "en-US", "BTX", "-55.00", ExpectedValidity.VALID_AMOUNT},

            {"55.5", "USD", "en-US", "USD", "$55.50", ExpectedValidity.VALID_AMOUNT},
            {"55", "USD", "en-US", "USD", "$55.00", ExpectedValidity.VALID_AMOUNT},
            {"123", "USD", "en-US", "USD", "$123.00", ExpectedValidity.VALID_AMOUNT},
            {"1234", "USD", "en-US", "USD", "$1,234.00", ExpectedValidity.VALID_AMOUNT},

            {"-123", "USD", "en-US", "USD", "-$123.00", ExpectedValidity.VALID_AMOUNT},
            {"-1234", "USD", "en-US", "USD", "-$1,234.00", ExpectedValidity.VALID_AMOUNT},

            {"123456789012345678901234567890.123456789012345678901234567890", "USD", "fr-FR", "USD",
                    "$123" + SPACE + "456" + SPACE + "789" + SPACE + "012" + SPACE + "345"
                            + SPACE + "678" + SPACE + "901" + SPACE + "234" + SPACE + "567"
                            + SPACE + "890,123456789012345678901234567890",
                    ExpectedValidity.VALID_AMOUNT},
            {"123456789012345678901234567890.123456789012345678901234567890", "USD", "en-US", "USD",
                    "$123,456,789,012,345,678,901,234,567,890.123456789012345678901234567890",
                    ExpectedValidity.VALID_AMOUNT},

            // Any string of at most 2048 characters can be valid amount currency codes.
            {"55.00", "", "en-US", "", "55.00", ExpectedValidity.VALID_AMOUNT},
            {"55.00", "ABCDEF", "en-US", "ABCDEF", "55.00", ExpectedValidity.VALID_AMOUNT},
            // Currency code more than 6 character is formatted to first 5 characters and ellipsis.
            // "\u2026" is unicode for ellipsis.
            {"55.00", longStringOfLength(2048), "en-US", "AAAAA\u2026", "55.00",
                    ExpectedValidity.VALID_AMOUNT},

            // Invalid amount currency codes.
            {"55.00", longStringOfLength(2049), "en-US", null, null,
                    ExpectedValidity.INVALID_AMOUNT_CURRENCY_CODE},

            // Invalid amount values.
            {"", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"-", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"notdigits", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"ALSONOTDIGITS", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"10.", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {".99", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"10-", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"1-0", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"1.0.0", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
            {"1/3", "USD", "en-US", "USD", null, ExpectedValidity.INVALID_AMOUNT_VALUE},
        });
    }

    private final String mAmount;
    private final String mCurrency;
    private final String mLanguageTag;
    private final String mExpectedCurrencyFormatting;
    private final String mExpectedAmountFormatting;
    private final ExpectedValidity mExpectedValidity;

    private static String longStringOfLength(int len) {
        StringBuilder currency = new StringBuilder();
        for (int i = 0; i < len; i++) {
            currency.append("A");
        }
        return currency.toString();
    }

    public CurrencyStringFormatterUnitTest(String amount, String currency, String languageTag,
            String expectedCurrencyFormatting, String expectedAmountFormatting,
            ExpectedValidity expectedValidity) {
        mAmount = amount;
        mCurrency = currency;
        mLanguageTag = languageTag;
        mExpectedCurrencyFormatting = expectedCurrencyFormatting;
        mExpectedAmountFormatting = expectedAmountFormatting;
        mExpectedValidity = expectedValidity;
    }

    @Test
    public void test() {
        CurrencyStringFormatter formatter = new CurrencyStringFormatter(mCurrency,
                Locale.forLanguageTag(mLanguageTag));

        if (mExpectedValidity == ExpectedValidity.INVALID_AMOUNT_CURRENCY_CODE) {
            Assert.assertFalse("\"" + mCurrency + "\" should be invalid currency code",
                    formatter.isValidAmountCurrencyCode(mCurrency));
        } else {
            Assert.assertTrue("\"" + mCurrency + "\" should be valid currency code",
                    formatter.isValidAmountCurrencyCode(mCurrency));
        }

        if (mExpectedValidity == ExpectedValidity.INVALID_AMOUNT_VALUE) {
            Assert.assertFalse("\"" + mAmount + "\" should be invalid amount value",
                    formatter.isValidAmountValue(mAmount));
        } else {
            Assert.assertTrue("\"" + mAmount + "\" should be valid amount value",
                    formatter.isValidAmountValue(mAmount));
        }

        if (mExpectedValidity == ExpectedValidity.VALID_AMOUNT) {
            Assert.assertEquals("\"" + mCurrency + "\" \"" + mAmount + "\" (\"" + mLanguageTag
                            + "\" locale) should be formatted into \""
                            + mExpectedAmountFormatting + "\"",
                    mExpectedAmountFormatting, formatter.format(mAmount));
            Assert.assertEquals("\"" + mCurrency + "\"" + " should be formatted into \""
                            + mExpectedCurrencyFormatting + "\"",
                    mExpectedCurrencyFormatting, formatter.getFormattedCurrencyCode());
        }
    }
}
