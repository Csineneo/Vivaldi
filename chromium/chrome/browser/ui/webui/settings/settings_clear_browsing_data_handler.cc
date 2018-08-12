// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/settings_clear_browsing_data_handler.h"

#include <stddef.h>

#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover_factory.h"
#include "chrome/browser/history/web_history_service_factory.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/browsing_data_ui/history_notice_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"

namespace settings {

ClearBrowsingDataHandler::ClearBrowsingDataHandler(content::WebUI* webui)
    : sync_service_(nullptr),
      sync_service_observer_(this),
      remover_(nullptr),
      should_show_history_footer_(false),
      weak_ptr_factory_(this) {
  PrefService* prefs = Profile::FromWebUI(webui)->GetPrefs();
  clear_plugin_lso_data_enabled_.Init(prefs::kClearPluginLSODataEnabled, prefs);
  pepper_flash_settings_enabled_.Init(prefs::kPepperFlashSettingsEnabled,
                                      prefs);
  sync_service_ =
        ProfileSyncServiceFactory::GetForProfile(Profile::FromWebUI(webui));
}

ClearBrowsingDataHandler::~ClearBrowsingDataHandler() {
  if (remover_)
    remover_->RemoveObserver(this);
}

void ClearBrowsingDataHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "clearBrowsingData",
      base::Bind(&ClearBrowsingDataHandler::HandleClearBrowsingData,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "initializeClearBrowsingData",
      base::Bind(&ClearBrowsingDataHandler::HandleInitialize,
                 base::Unretained(this)));
}

void ClearBrowsingDataHandler::OnJavascriptAllowed() {
  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
  allow_deleting_browser_history_.Init(
      prefs::kAllowDeletingBrowserHistory, prefs,
      base::Bind(&ClearBrowsingDataHandler::OnBrowsingHistoryPrefChanged,
                 base::Unretained(this)));

  if (sync_service_)
    sync_service_observer_.Add(sync_service_);
}

void ClearBrowsingDataHandler::OnJavascriptDisallowed() {
  allow_deleting_browser_history_.Destroy();
  sync_service_observer_.RemoveAll();
}

void ClearBrowsingDataHandler::HandleClearBrowsingData(
    const base::ListValue* args) {
  // We should never be called when the previous clearing has not yet finished.
  CHECK(!remover_);
  CHECK_EQ(1U, args->GetSize());
  CHECK(webui_callback_id_.empty());
  CHECK(args->GetString(0, &webui_callback_id_));

  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();

  int site_data_mask = BrowsingDataRemover::REMOVE_SITE_DATA;
  // Don't try to clear LSO data if it's not supported.
  if (!*clear_plugin_lso_data_enabled_)
    site_data_mask &= ~BrowsingDataRemover::REMOVE_PLUGIN_DATA;

  int remove_mask = 0;
  if (*allow_deleting_browser_history_) {
    if (prefs->GetBoolean(prefs::kDeleteBrowsingHistory))
      remove_mask |= BrowsingDataRemover::REMOVE_HISTORY;
    if (prefs->GetBoolean(prefs::kDeleteDownloadHistory))
      remove_mask |= BrowsingDataRemover::REMOVE_DOWNLOADS;
  }

  if (prefs->GetBoolean(prefs::kDeleteCache))
    remove_mask |= BrowsingDataRemover::REMOVE_CACHE;

  int origin_mask = 0;
  if (prefs->GetBoolean(prefs::kDeleteCookies)) {
    remove_mask |= site_data_mask;
    origin_mask |= BrowsingDataHelper::UNPROTECTED_WEB;
  }

  if (prefs->GetBoolean(prefs::kDeletePasswords))
    remove_mask |= BrowsingDataRemover::REMOVE_PASSWORDS;

  if (prefs->GetBoolean(prefs::kDeleteFormData))
    remove_mask |= BrowsingDataRemover::REMOVE_FORM_DATA;

  // Clearing Content Licenses is only supported in Pepper Flash.
  if (prefs->GetBoolean(prefs::kDeauthorizeContentLicenses) &&
      *pepper_flash_settings_enabled_) {
    remove_mask |= BrowsingDataRemover::REMOVE_CONTENT_LICENSES;
  }

  if (prefs->GetBoolean(prefs::kDeleteHostedAppsData)) {
    remove_mask |= site_data_mask;
    origin_mask |= BrowsingDataHelper::PROTECTED_WEB;
  }

  // Record the deletion of cookies and cache.
  BrowsingDataRemover::CookieOrCacheDeletionChoice choice =
      BrowsingDataRemover::NEITHER_COOKIES_NOR_CACHE;
  if (prefs->GetBoolean(prefs::kDeleteCookies)) {
    choice = prefs->GetBoolean(prefs::kDeleteCache)
                 ? BrowsingDataRemover::BOTH_COOKIES_AND_CACHE
                 : BrowsingDataRemover::ONLY_COOKIES;
  } else if (prefs->GetBoolean(prefs::kDeleteCache)) {
    choice = BrowsingDataRemover::ONLY_CACHE;
  }

  UMA_HISTOGRAM_ENUMERATION(
      "History.ClearBrowsingData.UserDeletedCookieOrCacheFromDialog", choice,
      BrowsingDataRemover::MAX_CHOICE_VALUE);

  // Record the circumstances under which passwords are deleted.
  if (prefs->GetBoolean(prefs::kDeletePasswords)) {
    static const char* other_types[] = {
        prefs::kDeleteBrowsingHistory,
        prefs::kDeleteDownloadHistory,
        prefs::kDeleteCache,
        prefs::kDeleteCookies,
        prefs::kDeleteFormData,
        prefs::kDeleteHostedAppsData,
        prefs::kDeauthorizeContentLicenses,
    };
    static size_t num_other_types = arraysize(other_types);
    int checked_other_types = std::count_if(
        other_types, other_types + num_other_types,
        [prefs](const std::string& pref) { return prefs->GetBoolean(pref); });
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "History.ClearBrowsingData.PasswordsDeletion.AdditionalDatatypesCount",
        checked_other_types);
  }

  int period_selected = prefs->GetInteger(prefs::kDeleteTimePeriod);
  remover_ = BrowsingDataRemoverFactory::GetForBrowserContext(profile);
  remover_->AddObserver(this);
  remover_->Remove(
      BrowsingDataRemover::Period(
          static_cast<BrowsingDataRemover::TimePeriod>(period_selected)),
      remove_mask, origin_mask);
}

void ClearBrowsingDataHandler::OnBrowsingDataRemoverDone() {
  remover_->RemoveObserver(this);
  remover_ = nullptr;
  ResolveJavascriptCallback(
      base::StringValue(webui_callback_id_),
      *base::Value::CreateNullValue());
  webui_callback_id_.clear();
}

void ClearBrowsingDataHandler::OnBrowsingHistoryPrefChanged() {
  CallJavascriptFunction(
      "cr.webUIListenerCallback",
      base::StringValue("browsing-history-pref-changed"),
      base::FundamentalValue(*allow_deleting_browser_history_));
}

void ClearBrowsingDataHandler::HandleInitialize(const base::ListValue* args) {
  AllowJavascript();
  OnStateChanged();
  RefreshHistoryNotice();
}

void ClearBrowsingDataHandler::OnStateChanged() {
  CallJavascriptFunction(
      "cr.webUIListenerCallback",
      base::StringValue("update-footer"),
      base::FundamentalValue(sync_service_ && sync_service_->IsSyncActive()),
      base::FundamentalValue(should_show_history_footer_));
}

void ClearBrowsingDataHandler::RefreshHistoryNotice() {
  browsing_data_ui::ShouldShowNoticeAboutOtherFormsOfBrowsingHistory(
      sync_service_,
      WebHistoryServiceFactory::GetForProfile(Profile::FromWebUI(web_ui())),
      base::Bind(&ClearBrowsingDataHandler::UpdateHistoryNotice,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ClearBrowsingDataHandler::UpdateHistoryNotice(bool show) {
  should_show_history_footer_ = show;
  OnStateChanged();

  UMA_HISTOGRAM_BOOLEAN(
      "History.ClearBrowsingData.HistoryNoticeShownInFooterWhenUpdated",
      should_show_history_footer_);
}

}  // namespace settings
