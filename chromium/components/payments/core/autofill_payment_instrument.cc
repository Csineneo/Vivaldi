// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/autofill_payment_instrument.h"

#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/payments/core/basic_card_response.h"
#include "components/payments/core/payment_request_data_util.h"
#include "components/payments/core/payment_request_delegate.h"

namespace payments {

namespace {

// Returns whether |card| has a non-empty number and cardholder name. Server
// cards will have a non-empty number.
bool CreditCardHasNumberAndName(const autofill::CreditCard& card,
                                const std::string& app_locale) {
  return !card.number().empty() &&
         !card.GetInfo(autofill::AutofillType(autofill::CREDIT_CARD_NAME_FULL),
                       app_locale)
              .empty();
}

}  // namespace

AutofillPaymentInstrument::AutofillPaymentInstrument(
    const std::string& method_name,
    const autofill::CreditCard& card,
    const std::vector<autofill::AutofillProfile*>& billing_profiles,
    const std::string& app_locale,
    PaymentRequestDelegate* payment_request_delegate)
    : PaymentInstrument(
          method_name,
          /* label= */ card.TypeAndLastFourDigits(),
          /* sublabel= */
          card.GetInfo(autofill::AutofillType(autofill::CREDIT_CARD_NAME_FULL),
                       app_locale),
          autofill::data_util::GetPaymentRequestData(card.type())
              .icon_resource_id,
          PaymentInstrument::Type::AUTOFILL),
      credit_card_(card),
      billing_profiles_(billing_profiles),
      app_locale_(app_locale),
      delegate_(nullptr),
      payment_request_delegate_(payment_request_delegate),
      weak_ptr_factory_(this) {}
AutofillPaymentInstrument::~AutofillPaymentInstrument() {}

void AutofillPaymentInstrument::InvokePaymentApp(
    PaymentInstrument::Delegate* delegate) {
  DCHECK(delegate);
  // There can be only one FullCardRequest going on at a time. If |delegate_| is
  // not null, there's already an active request, which shouldn't happen.
  // |delegate_| is reset to nullptr when the request succeeds or fails.
  DCHECK(!delegate_);
  delegate_ = delegate;

  payment_request_delegate_->DoFullCardRequest(credit_card_,
                                               weak_ptr_factory_.GetWeakPtr());
}

bool AutofillPaymentInstrument::IsCompleteForPayment() {
  // A card is complete for payment if it's not expired, its number is not
  // empty (a server card fills this condition) and there is a cardholder name.
  // TODO(crbug.com/709776): Check for billing address association.
  return !credit_card_.IsExpired(autofill::AutofillClock::Now()) &&
         CreditCardHasNumberAndName(credit_card_, app_locale_);
}

bool AutofillPaymentInstrument::IsValidForCanMakePayment() {
  // An expired card is still valid for the purposes of canMakePayment.
  return CreditCardHasNumberAndName(credit_card_, app_locale_);
}

void AutofillPaymentInstrument::OnFullCardRequestSucceeded(
    const autofill::CreditCard& card,
    const base::string16& cvc) {
  DCHECK(delegate_);
  credit_card_ = card;
  std::unique_ptr<base::DictionaryValue> response_value =
      payments::data_util::GetBasicCardResponseFromAutofillCreditCard(
          credit_card_, cvc, billing_profiles_, app_locale_)
          .ToDictionaryValue();
  std::string stringified_details;
  base::JSONWriter::Write(*response_value, &stringified_details);
  delegate_->OnInstrumentDetailsReady(method_name(), stringified_details);
  delegate_ = nullptr;
}

void AutofillPaymentInstrument::OnFullCardRequestFailed() {
  // TODO(anthonyvd): Do something with the error.
  delegate_ = nullptr;
}

}  // namespace payments
