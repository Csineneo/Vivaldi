// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_PAYMENTS_CREDIT_CARD_EDIT_MEDIATOR_H_
#define IOS_CHROME_BROWSER_PAYMENTS_CREDIT_CARD_EDIT_MEDIATOR_H_

#import "ios/chrome/browser/payments/credit_card_edit_view_controller.h"

class PaymentRequest;

namespace autofill {
class CreditCard;
}  // namespace autofill

// Serves as data source for CreditCardEditViewController.
@interface CreditCardEditViewControllerMediator
    : NSObject<CreditCardEditViewControllerDataSource>

// Initializes this object with an instance of PaymentRequest which has a copy
// of web::PaymentRequest as provided by the page invoking the Payment Request
// API as well as |creditCard| which is the credit card to be edited, if any.
// This object will not take ownership of |paymentRequest| or |creditCard|.
- (instancetype)initWithPaymentRequest:(PaymentRequest*)paymentRequest
                            creditCard:(autofill::CreditCard*)creditCard
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

@end

#endif  // IOS_CHROME_BROWSER_PAYMENTS_CREDIT_CARD_EDIT_MEDIATOR_H_
