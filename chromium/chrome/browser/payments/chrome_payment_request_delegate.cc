// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/payments/chrome_payment_request_delegate.h"

#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/autofill/validation_rules_storage_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/payments/content/payment_request_dialog.h"
#include "content/public/browser/web_contents.h"
#include "third_party/libaddressinput/chromium/chrome_metadata_source.h"
#include "third_party/libaddressinput/chromium/chrome_storage_impl.h"

namespace payments {

ChromePaymentRequestDelegate::ChromePaymentRequestDelegate(
    content::WebContents* web_contents)
    : dialog_(nullptr), web_contents_(web_contents) {}

void ChromePaymentRequestDelegate::ShowDialog(PaymentRequest* request) {
  DCHECK_EQ(nullptr, dialog_);
  dialog_ = chrome::CreatePaymentRequestDialog(request);
  dialog_->ShowDialog();
}

void ChromePaymentRequestDelegate::CloseDialog() {
  if (dialog_) {
    dialog_->CloseDialog();
    dialog_ = nullptr;
  }
}

void ChromePaymentRequestDelegate::ShowErrorMessage() {
  if (dialog_)
    dialog_->ShowErrorMessage();
}

autofill::PersonalDataManager*
ChromePaymentRequestDelegate::GetPersonalDataManager() {
  return autofill::PersonalDataManagerFactory::GetForProfile(
      Profile::FromBrowserContext(web_contents_->GetBrowserContext()));
}

const std::string& ChromePaymentRequestDelegate::GetApplicationLocale() const {
  return g_browser_process->GetApplicationLocale();
}

bool ChromePaymentRequestDelegate::IsIncognito() const {
  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  return profile && profile->GetProfileType() == Profile::INCOGNITO_PROFILE;
}

void ChromePaymentRequestDelegate::DoFullCardRequest(
    const autofill::CreditCard& credit_card,
    base::WeakPtr<autofill::payments::FullCardRequest::ResultDelegate>
        result_delegate) {
  dialog_->ShowCvcUnmaskPrompt(credit_card, result_delegate, web_contents_);
}

std::unique_ptr<const ::i18n::addressinput::Source>
ChromePaymentRequestDelegate::GetAddressInputSource() {
  return base::WrapUnique(new autofill::ChromeMetadataSource(
      I18N_ADDRESS_VALIDATION_DATA_URL,
      GetPersonalDataManager()->GetURLRequestContextGetter()));
}
std::unique_ptr<::i18n::addressinput::Storage>
ChromePaymentRequestDelegate::GetAddressInputStorage() {
  return autofill::ValidationRulesStorageFactory::CreateStorage();
}

}  // namespace payments
