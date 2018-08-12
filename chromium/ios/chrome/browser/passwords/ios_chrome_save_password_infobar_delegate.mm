// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/passwords/ios_chrome_save_password_infobar_delegate.h"

#include <utility>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram.h"
#include "base/strings/string16.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_manager.h"
#include "components/password_manager/core/browser/password_form_manager.h"
#include "components/password_manager/core/browser/password_manager_constants.h"
#include "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/ui/commands/UIKit+ChromeExecuteCommand.h"
#import "ios/chrome/browser/ui/commands/open_url_command.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#include "ios/chrome/grit/ios_google_chrome_strings.h"
#include "ios/chrome/grit/ios_strings.h"
#include "ios/chrome/grit/ios_theme_resources.h"
#include "ios/web/public/referrer.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

using password_manager::PasswordFormManager;

// static
void IOSChromeSavePasswordInfoBarDelegate::Create(
    bool is_smart_lock_branding_enabled,
    infobars::InfoBarManager* infobar_manager,
    std::unique_ptr<PasswordFormManager> form_to_save) {
  DCHECK(infobar_manager);
  auto delegate = base::WrapUnique(new IOSChromeSavePasswordInfoBarDelegate(
      is_smart_lock_branding_enabled, std::move(form_to_save)));
  infobar_manager->AddInfoBar(
      infobar_manager->CreateConfirmInfoBar(std::move(delegate)));
}

IOSChromeSavePasswordInfoBarDelegate::~IOSChromeSavePasswordInfoBarDelegate() {
  password_manager::metrics_util::LogUIDismissalReason(infobar_response_);
}

IOSChromeSavePasswordInfoBarDelegate::IOSChromeSavePasswordInfoBarDelegate(
    bool is_smart_lock_branding_enabled,
    std::unique_ptr<PasswordFormManager> form_to_save)
    : form_to_save_(std::move(form_to_save)),
      infobar_response_(password_manager::metrics_util::NO_DIRECT_INTERACTION),
      is_smart_lock_branding_enabled_(is_smart_lock_branding_enabled) {}

infobars::InfoBarDelegate::Type
IOSChromeSavePasswordInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

infobars::InfoBarDelegate::InfoBarIdentifier
IOSChromeSavePasswordInfoBarDelegate::GetIdentifier() const {
  return IOS_CHROME_SAVE_PASSWORD_INFOBAR_DELEGATE;
}

base::string16 IOSChromeSavePasswordInfoBarDelegate::GetMessageText() const {
  if (is_smart_lock_branding_enabled_) {
    return l10n_util::GetStringUTF16(
        IDS_IOS_PASSWORD_MANAGER_SAVE_PASSWORD_PROMPT_SMART_LOCK_BRANDING);
  }
  return l10n_util::GetStringUTF16(
      IDS_IOS_PASSWORD_MANAGER_SAVE_PASSWORD_PROMPT);
}

base::string16 IOSChromeSavePasswordInfoBarDelegate::GetLinkText() const {
  return is_smart_lock_branding_enabled_
             ? l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_SMART_LOCK)
             : base::string16();
}

base::string16 IOSChromeSavePasswordInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16(
      (button == BUTTON_OK) ? IDS_IOS_PASSWORD_MANAGER_SAVE_BUTTON
                            : IDS_IOS_PASSWORD_MANAGER_BLACKLIST_BUTTON);
}

bool IOSChromeSavePasswordInfoBarDelegate::Accept() {
  DCHECK(form_to_save_);
  form_to_save_->Save();
  infobar_response_ = password_manager::metrics_util::CLICKED_SAVE;
  return true;
}

bool IOSChromeSavePasswordInfoBarDelegate::Cancel() {
  DCHECK(form_to_save_);
  form_to_save_->PermanentlyBlacklist();
  infobar_response_ = password_manager::metrics_util::CLICKED_NEVER;
  return true;
}

bool IOSChromeSavePasswordInfoBarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
  base::scoped_nsobject<OpenUrlCommand> command([[OpenUrlCommand alloc]
       initWithURL:GURL(password_manager::kPasswordManagerHelpCenterSmartLock)
          referrer:web::Referrer()
        windowName:nil
       inIncognito:NO
      inBackground:NO
          appendTo:kCurrentTab]);
  UIWindow* mainWindow = [[UIApplication sharedApplication] keyWindow];
  [mainWindow chromeExecuteCommand:command];
  return true;
}

int IOSChromeSavePasswordInfoBarDelegate::GetIconId() const {
  return IDR_IOS_INFOBAR_SAVE_PASSWORD;
}
