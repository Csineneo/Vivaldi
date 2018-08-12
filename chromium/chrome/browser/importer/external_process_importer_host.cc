// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/importer/external_process_importer_host.h"

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/importer/external_process_importer_client.h"
#include "chrome/browser/importer/firefox_profile_lock.h"
#include "chrome/browser/importer/importer_lock_dialog.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "chrome/browser/importer/in_process_importer_bridge.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/browser_thread.h"

#include "app/vivaldi_resources.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/common/importer/importer_type.h"
#include "ui/base/l10n/l10n_util.h"

using bookmarks::BookmarkModel;
using content::BrowserThread;

ExternalProcessImporterHost::ExternalProcessImporterHost()
    : headless_(false),
      parent_window_(NULL),
      observer_(NULL),
      profile_(NULL),
      waiting_for_bookmarkbar_model_(false),
      installed_bookmark_observer_(false),
      is_source_readable_(true),
      client_(NULL),
      cancelled_(false),
      weak_ptr_factory_(this) {
}

void ExternalProcessImporterHost::Cancel() {
  cancelled_ = true;
  // There is only a |client_| if the import was started.
  if (client_)
    client_->Cancel();
  NotifyImportEnded();  // Tells the observer that we're done, and deletes us.
}

void ExternalProcessImporterHost::StartImportSettings(
      const importer::SourceProfile& source_profile,
      Profile* target_profile,
      uint16_t imported_items,
      ProfileWriter* writer)
{
  importer::ImportConfig import_config;

  import_config.imported_items = imported_items;
  if (!source_profile.master_password.empty()) {
    import_config.arguments.resize(1);
    import_config.arguments[0] =
        base::UTF8ToUTF16(source_profile.master_password);
  }

  StartImportSettings(source_profile, target_profile, import_config, writer);
}

void ExternalProcessImporterHost::StartImportSettings(
    const importer::SourceProfile& source_profile,
    Profile* target_profile,
    const importer::ImportConfig &import_config,
    ProfileWriter* writer) {
  // We really only support importing from one host at a time.
  DCHECK(!profile_);
  DCHECK(target_profile);

  profile_ = target_profile;
  writer_ = writer;
  source_profile_ = source_profile;
  import_config_ = import_config;

  if (!CheckForFirefoxLock(source_profile)) {
    Cancel();
    return;
  }

  if (!CheckForChromeLock(source_profile)) {
    Cancel();
    return;
  }

  CheckForLoadedModels(import_config_.imported_items);

  LaunchImportIfReady();
}

void ExternalProcessImporterHost::NotifyImportStarted() {
  if (observer_)
    observer_->ImportStarted();
}

void ExternalProcessImporterHost::NotifyImportItemStarted(
    importer::ImportItem item) {
  if (observer_)
    observer_->ImportItemStarted(item);
}

void ExternalProcessImporterHost::NotifyImportItemEnded(
    importer::ImportItem item) {
  if (observer_)
    observer_->ImportItemEnded(item);
}

void ExternalProcessImporterHost::NotifyImportItemFailed(
    importer::ImportItem item,
    const std::string& error) {
  if (observer_)
    observer_->ImportItemFailed(item, error);
}

void ExternalProcessImporterHost::NotifyImportEnded() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  firefox_lock_.reset();
  chromium_lock_.reset();

  if (observer_)
    observer_->ImportEnded();
  delete this;
}

ExternalProcessImporterHost::~ExternalProcessImporterHost() {
  if (installed_bookmark_observer_) {
    DCHECK(profile_);
    BookmarkModelFactory::GetForBrowserContext(profile_)->RemoveObserver(this);
  }
}

void ExternalProcessImporterHost::LaunchImportIfReady() {
  if (waiting_for_bookmarkbar_model_ || template_service_subscription_.get() ||
      !is_source_readable_ || cancelled_)
    return;

  // This is the in-process half of the bridge, which catches data from the IPC
  // pipe and feeds it to the ProfileWriter. The external process half of the
  // bridge lives in the external process (see ProfileImportThread).
  // The ExternalProcessImporterClient created in the next line owns the bridge,
  // and will delete it.
  InProcessImporterBridge* bridge =
      new InProcessImporterBridge(writer_.get(),
                                  weak_ptr_factory_.GetWeakPtr());
  client_ = new ExternalProcessImporterClient(
      weak_ptr_factory_.GetWeakPtr(), source_profile_, import_config_, bridge);
  client_->Start();
}

void ExternalProcessImporterHost::BookmarkModelLoaded(BookmarkModel* model,
                                                      bool ids_reassigned) {
  DCHECK(model->loaded());
  model->RemoveObserver(this);
  waiting_for_bookmarkbar_model_ = false;
  installed_bookmark_observer_ = false;

  LaunchImportIfReady();
}

void ExternalProcessImporterHost::BookmarkModelBeingDeleted(
    BookmarkModel* model) {
  installed_bookmark_observer_ = false;
}

void ExternalProcessImporterHost::BookmarkModelChanged() {
}

void ExternalProcessImporterHost::OnTemplateURLServiceLoaded() {
  template_service_subscription_.reset();
  LaunchImportIfReady();
}

void ExternalProcessImporterHost::ShowWarningDialog() {
  DCHECK(!headless_);
  importer::ShowImportLockDialog(
      parent_window_,
      base::Bind(&ExternalProcessImporterHost::OnImportLockDialogEnd,
                 weak_ptr_factory_.GetWeakPtr()),
                 l10n_util::GetStringUTF16(IDS_IMPORTER_LOCK_TEXT));
}

void ExternalProcessImporterHost::ShowChromeWarningDialog() {
  base::string16 importerLockString;

  if (source_profile_.importer_type == importer::TYPE_CHROME ||
      source_profile_.importer_type == importer::TYPE_CHROMIUM) {
    importerLockString =
        l10n_util::GetStringUTF16(IDS_CHROME_IMPORTER_LOCK_TEXT);
  } else if (source_profile_.importer_type == importer::TYPE_OPERA_OPIUM ||
             source_profile_.importer_type == importer::TYPE_OPERA_OPIUM_BETA ||
             source_profile_.importer_type == importer::TYPE_OPERA_OPIUM_DEV) {
    importerLockString =
        l10n_util::GetStringUTF16(IDS_OPIUM_IMPORTER_LOCK_TEXT);
  } else if (source_profile_.importer_type == importer::TYPE_YANDEX) {
    importerLockString =
        l10n_util::GetStringUTF16(IDS_YANDEX_IMPORTER_LOCK_TEXT);
  }

  DCHECK(!headless_);
  importer::ShowImportLockDialog(
    parent_window_,
    base::Bind(&ExternalProcessImporterHost::OnChromiumImportLockDialogEnd,
    weak_ptr_factory_.GetWeakPtr()),
    importerLockString);
}

void ExternalProcessImporterHost::OnChromiumImportLockDialogEnd(
    bool is_continue) {
  if (is_continue) {
    // User chose to continue, then we check the lock again to make
    // sure that Chromium has been closed. Try to import the settings
    // if successful. Otherwise, show a warning dialog.
    chromium_lock_->Lock();
    if (chromium_lock_->HasAcquired()) {
      is_source_readable_ = true;
      LaunchImportIfReady();
    } else {
      ShowChromeWarningDialog();
    }
  } else {
    NotifyImportEnded();
  }
}

void ExternalProcessImporterHost::OnImportLockDialogEnd(bool is_continue) {
  if (is_continue) {
    // User chose to continue, then we check the lock again to make
    // sure that Firefox has been closed. Try to import the settings
    // if successful. Otherwise, show a warning dialog.
    firefox_lock_->Lock();
    if (firefox_lock_->HasAcquired()) {
      is_source_readable_ = true;
      LaunchImportIfReady();
    } else {
      ShowWarningDialog();
    }
  } else {
    NotifyImportEnded();
  }
}

bool ExternalProcessImporterHost::CheckForChromeLock(
  const importer::SourceProfile& source_profile) {
  if (source_profile.importer_type != importer::TYPE_CHROME &&
      source_profile.importer_type != importer::TYPE_YANDEX &&
      source_profile.importer_type != importer::TYPE_OPERA_OPIUM &&
      source_profile.importer_type != importer::TYPE_OPERA_OPIUM_BETA &&
      source_profile.importer_type != importer::TYPE_OPERA_OPIUM_DEV &&
      source_profile.importer_type != importer::TYPE_VIVALDI)
    return true;

  DCHECK(!chromium_lock_.get());
  chromium_lock_.reset(new ChromiumProfileLock(source_profile.source_path));
  if (chromium_lock_->HasAcquired())
    return true;

  // If fail to acquire the lock, we set the source unreadable and
  // show a warning dialog, unless running without UI (in which case the import
  // must be aborted).
  is_source_readable_ = false;
  if (headless_)
    return false;

  ShowChromeWarningDialog();
  return true;
}

bool ExternalProcessImporterHost::CheckForFirefoxLock(
    const importer::SourceProfile& source_profile) {
  if (source_profile.importer_type != importer::TYPE_FIREFOX)
    return true;

  DCHECK(!firefox_lock_.get());
  firefox_lock_.reset(new FirefoxProfileLock(source_profile.source_path));
  if (firefox_lock_->HasAcquired())
    return true;

  // If fail to acquire the lock, we set the source unreadable and
  // show a warning dialog, unless running without UI (in which case the import
  // must be aborted).
  is_source_readable_ = false;
  if (headless_)
    return false;

  ShowWarningDialog();
  return true;
}

void ExternalProcessImporterHost::CheckForLoadedModels(uint16_t items) {
  // A target profile must be loaded by StartImportSettings().
  DCHECK(profile_);

  // BookmarkModel should be loaded before adding IE favorites. So we observe
  // the BookmarkModel if needed, and start the task after it has been loaded.
  if ((items & importer::FAVORITES) && !writer_->BookmarkModelIsLoaded()) {
    BookmarkModelFactory::GetForBrowserContext(profile_)->AddObserver(this);
    waiting_for_bookmarkbar_model_ = true;
    installed_bookmark_observer_ = true;
  }

  // Observes the TemplateURLService if needed to import search engines from the
  // other browser. We also check to see if we're importing bookmarks because
  // we can import bookmark keywords from Firefox as search engines.
  if ((items & importer::SEARCH_ENGINES) || (items & importer::FAVORITES)) {
    if (!writer_->TemplateURLServiceIsLoaded()) {
      TemplateURLService* model =
          TemplateURLServiceFactory::GetForProfile(profile_);
      template_service_subscription_ = model->RegisterOnLoadedCallback(
          base::Bind(&ExternalProcessImporterHost::OnTemplateURLServiceLoaded,
                     weak_ptr_factory_.GetWeakPtr()));
      model->Load();
    }
  }
}
