// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/autofill_payment_instrument.h"

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/credit_card.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace payments {

class AutofillPaymentInstrumentTest : public testing::Test {
 protected:
  AutofillPaymentInstrumentTest()
      : address_(autofill::test::GetFullProfile()),
        local_card_(autofill::test::GetCreditCard()),
        billing_profiles_({&address_}) {
    local_card_.set_billing_address_id(address_.guid());
  }

  autofill::CreditCard& local_credit_card() { return local_card_; }
  const std::vector<autofill::AutofillProfile*>& billing_profiles() {
    return billing_profiles_;
  }

 private:
  autofill::AutofillProfile address_;
  autofill::CreditCard local_card_;
  std::vector<autofill::AutofillProfile*> billing_profiles_;

  DISALLOW_COPY_AND_ASSIGN(AutofillPaymentInstrumentTest);
};

// A valid local credit card is a valid instrument for payment.
TEST_F(AutofillPaymentInstrumentTest, IsCompleteForPayment) {
  AutofillPaymentInstrument instrument("visa", local_credit_card(),
                                       billing_profiles(), "en-US", nullptr);
  EXPECT_TRUE(instrument.IsCompleteForPayment());
}

// An expired local card is not a valid instrument for payment.
TEST_F(AutofillPaymentInstrumentTest, IsCompleteForPayment_Expired) {
  autofill::CreditCard& card = local_credit_card();
  card.SetExpirationYear(2016);  // Expired.
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_FALSE(instrument.IsCompleteForPayment());
}

// A local card with no name is not a valid instrument for payment.
TEST_F(AutofillPaymentInstrumentTest, IsCompleteForPayment_NoName) {
  autofill::CreditCard& card = local_credit_card();
  card.SetInfo(autofill::AutofillType(autofill::CREDIT_CARD_NAME_FULL),
               base::ASCIIToUTF16(""), "en-US");
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_FALSE(instrument.IsCompleteForPayment());
}

// A local card with no name is not a valid instrument for payment.
TEST_F(AutofillPaymentInstrumentTest, IsCompleteForPayment_NoNumber) {
  autofill::CreditCard& card = local_credit_card();
  card.SetNumber(base::ASCIIToUTF16(""));
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_FALSE(instrument.IsCompleteForPayment());
}

// A Masked (server) card is a valid instrument for payment.
TEST_F(AutofillPaymentInstrumentTest, IsCompleteForPayment_MaskedCard) {
  autofill::CreditCard card = autofill::test::GetMaskedServerCard();
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_TRUE(instrument.IsCompleteForPayment());
}

// An expired masked (server) card is not a valid instrument for payment.
TEST_F(AutofillPaymentInstrumentTest, IsCompleteForPayment_ExpiredMaskedCard) {
  autofill::CreditCard card = autofill::test::GetMaskedServerCard();
  card.SetExpirationYear(2016);  // Expired.
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_FALSE(instrument.IsCompleteForPayment());
}

// An expired card is a valid instrument for canMakePayment.
TEST_F(AutofillPaymentInstrumentTest, IsValidForCanMakePayment_Minimal) {
  autofill::CreditCard& card = local_credit_card();
  card.SetExpirationYear(2016);  // Expired.
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_TRUE(instrument.IsValidForCanMakePayment());
}

// An expired Masked (server) card is a valid instrument for canMakePayment.
TEST_F(AutofillPaymentInstrumentTest, IsValidForCanMakePayment_MaskedCard) {
  autofill::CreditCard card = autofill::test::GetMaskedServerCard();
  card.SetExpirationYear(2016);  // Expired.
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_TRUE(instrument.IsValidForCanMakePayment());
}

// A card with no name is not a valid instrument for canMakePayment.
TEST_F(AutofillPaymentInstrumentTest, IsValidForCanMakePayment_NoName) {
  autofill::CreditCard& card = local_credit_card();
  card.SetInfo(autofill::AutofillType(autofill::CREDIT_CARD_NAME_FULL),
               base::ASCIIToUTF16(""), "en-US");
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_FALSE(instrument.IsValidForCanMakePayment());
}

// A card with no number is not a valid instrument for canMakePayment.
TEST_F(AutofillPaymentInstrumentTest, IsValidForCanMakePayment_NoNumber) {
  autofill::CreditCard& card = local_credit_card();
  card.SetNumber(base::ASCIIToUTF16(""));
  AutofillPaymentInstrument instrument("visa", card, billing_profiles(),
                                       "en-US", nullptr);
  EXPECT_FALSE(instrument.IsValidForCanMakePayment());
}

}  // namespace payments
