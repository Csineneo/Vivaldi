// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/test_chrome_payment_request_delegate.h"

#include <utility>

#include "content/public/browser/web_contents.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace payments {

TestChromePaymentRequestDelegate::TestChromePaymentRequestDelegate(
    content::WebContents* web_contents,
    PaymentRequestDialogView::ObserverForTest* observer,
    views::WidgetObserver* widget_observer,
    bool is_incognito)
    : ChromePaymentRequestDelegate(web_contents),
      address_input_provider_(nullptr),
      observer_(observer),
      widget_observer_(widget_observer),
      is_incognito_for_testing_(is_incognito) {}

void TestChromePaymentRequestDelegate::ShowDialog(PaymentRequest* request) {
  PaymentRequestDialogView* dialog_view =
      new PaymentRequestDialogView(request, observer_);
  dialog_view->ShowDialog();

  // The widget is now valid, so register its observer.
  views::Widget* widget = dialog_view->GetWidget();
  widget->AddObserver(widget_observer_);

  dialog_ = std::move(dialog_view);
}

bool TestChromePaymentRequestDelegate::IsIncognito() const {
  return is_incognito_for_testing_;
}

std::unique_ptr<const ::i18n::addressinput::Source>
TestChromePaymentRequestDelegate::GetAddressInputSource() {
  if (address_input_provider_)
    return address_input_provider_->GetAddressInputSource();
  return ChromePaymentRequestDelegate::GetAddressInputSource();
}

std::unique_ptr<::i18n::addressinput::Storage>
TestChromePaymentRequestDelegate::GetAddressInputStorage() {
  if (address_input_provider_)
    return address_input_provider_->GetAddressInputStorage();
  return ChromePaymentRequestDelegate::GetAddressInputStorage();
}

}  // namespace payments
