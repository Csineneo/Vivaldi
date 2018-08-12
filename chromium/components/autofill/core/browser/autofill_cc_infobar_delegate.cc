// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_cc_infobar_delegate.h"

#include "base/logging.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_manager.h"
#include "grit/components_scaled_resources.h"
#include "grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/vector_icons_public.h"
#include "url/gurl.h"

namespace autofill {

// static
void AutofillCCInfoBarDelegate::CreateForLocalSave(
    infobars::InfoBarManager* infobar_manager,
    const base::Closure& save_card_callback) {
  infobar_manager->AddInfoBar(
      infobar_manager->CreateConfirmInfoBar(scoped_ptr<ConfirmInfoBarDelegate>(
          new AutofillCCInfoBarDelegate(false, save_card_callback))));
}

// static
void AutofillCCInfoBarDelegate::CreateForUpload(
    infobars::InfoBarManager* infobar_manager,
    const base::Closure& save_card_callback) {
  infobar_manager->AddInfoBar(
      infobar_manager->CreateConfirmInfoBar(scoped_ptr<ConfirmInfoBarDelegate>(
          new AutofillCCInfoBarDelegate(true, save_card_callback))));
}

AutofillCCInfoBarDelegate::~AutofillCCInfoBarDelegate() {
  if (!had_user_interaction_)
    LogUserAction(AutofillMetrics::INFOBAR_IGNORED);
}

int AutofillCCInfoBarDelegate::GetIconId() const {
  return IDR_INFOBAR_AUTOFILL_CC;
}

base::string16 AutofillCCInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(
      upload_ ? IDS_AUTOFILL_SAVE_CARD_PROMPT_TITLE_TO_CLOUD
              : IDS_AUTOFILL_SAVE_CARD_PROMPT_TITLE_LOCAL);
}

base::string16 AutofillCCInfoBarDelegate::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

AutofillCCInfoBarDelegate::AutofillCCInfoBarDelegate(
    bool upload,
    const base::Closure& save_card_callback)
    : ConfirmInfoBarDelegate(),
      upload_(upload),
      save_card_callback_(save_card_callback),
      had_user_interaction_(false) {
  AutofillMetrics::LogCreditCardInfoBarMetric(AutofillMetrics::INFOBAR_SHOWN);
}

void AutofillCCInfoBarDelegate::LogUserAction(
    AutofillMetrics::InfoBarMetric user_action) {
  DCHECK(!had_user_interaction_);

  AutofillMetrics::LogCreditCardInfoBarMetric(user_action);
  had_user_interaction_ = true;
}

infobars::InfoBarDelegate::InfoBarIdentifier
AutofillCCInfoBarDelegate::GetIdentifier() const {
  return AUTOFILL_CC_INFOBAR_DELEGATE;
}

infobars::InfoBarDelegate::Type
AutofillCCInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

gfx::VectorIconId AutofillCCInfoBarDelegate::GetVectorIconId() const {
#if !defined(OS_MACOSX) && !defined(OS_IOS) && !defined(OS_ANDROID)
  return gfx::VectorIconId::AUTOFILL;
#else
  return gfx::VectorIconId::VECTOR_ICON_NONE;
#endif
}

bool AutofillCCInfoBarDelegate::ShouldExpire(
    const NavigationDetails& details) const {
  // The user has submitted a form, causing the page to navigate elsewhere. We
  // don't want the infobar to be expired at this point, because the user won't
  // get a chance to answer the question.
  return false;
}

void AutofillCCInfoBarDelegate::InfoBarDismissed() {
  LogUserAction(AutofillMetrics::INFOBAR_DENIED);
}

base::string16 AutofillCCInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16(button == BUTTON_OK
                                       ? IDS_AUTOFILL_SAVE_CARD_PROMPT_ACCEPT
                                       : IDS_AUTOFILL_SAVE_CARD_PROMPT_DENY);
}

bool AutofillCCInfoBarDelegate::Accept() {
  save_card_callback_.Run();
  save_card_callback_.Reset();
  LogUserAction(AutofillMetrics::INFOBAR_ACCEPTED);
  return true;
}

bool AutofillCCInfoBarDelegate::Cancel() {
  LogUserAction(AutofillMetrics::INFOBAR_DENIED);
  return true;
}

GURL AutofillCCInfoBarDelegate::GetLinkURL() const {
  return GURL(kHelpURL);
}

}  // namespace autofill
