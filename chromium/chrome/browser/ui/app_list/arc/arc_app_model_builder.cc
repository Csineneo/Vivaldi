// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/arc/arc_app_model_builder.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_item.h"

ArcAppModelBuilder::ArcAppModelBuilder(AppListControllerDelegate* controller)
    : AppListModelBuilder(controller, ArcAppItem::kItemType) {
}

ArcAppModelBuilder::~ArcAppModelBuilder() {
  prefs_->RemoveObserver(this);
}

void ArcAppModelBuilder::BuildModel() {
  prefs_ = ArcAppListPrefs::Get(profile());

  std::vector<std::string> app_ids = prefs_->GetAppIds();
  for (auto& app_id : app_ids) {
    scoped_ptr<ArcAppListPrefs::AppInfo> app_info = prefs_->GetApp(app_id);
    if (!app_info)
      continue;

    InsertApp(CreateApp(app_id, *app_info));
  }

  prefs_->AddObserver(this);
}

ArcAppItem* ArcAppModelBuilder::GetArcAppItem(const std::string& app_id) {
  return static_cast<ArcAppItem*>(GetAppItem(app_id));
}

scoped_ptr<ArcAppItem> ArcAppModelBuilder::CreateApp(
    const std::string& app_id,
    const ArcAppListPrefs::AppInfo& app_info) {
  return make_scoped_ptr(new ArcAppItem(profile(),
                                        GetSyncItem(app_id),
                                        app_id,
                                        app_info.name,
                                        app_info.ready));
}

void ArcAppModelBuilder::OnAppRegistered(
    const std::string& app_id,
    const ArcAppListPrefs::AppInfo& app_info) {
  InsertApp(CreateApp(app_id, app_info));
}

void ArcAppModelBuilder::OnAppReadyChanged(const std::string& app_id,
                                           bool ready) {
  ArcAppItem* app_item = GetArcAppItem(app_id);
  if (!app_item) {
    VLOG(2) << "Could not update the state of ARC app(" << app_id
            << ") because it was not found.";
    return;
  }

  app_item->SetReady(ready);
}

void ArcAppModelBuilder::OnAppRemoved(const std::string& app_id) {
  RemoveApp(app_id);
}

void ArcAppModelBuilder::OnAppIconUpdated(const std::string& app_id,
                                          ui::ScaleFactor scale_factor) {
  ArcAppItem* app_item = GetArcAppItem(app_id);
  if (!app_item) {
    VLOG(2) << "Could not update the icon of ARC app(" << app_id
            << ") because it was not found.";
    return;
  }

  // Initiate async icon reloading.
  app_item->arc_app_icon()->LoadForScaleFactor(scale_factor);
}

void ArcAppModelBuilder::OnAppNameUpdated(const std::string& app_id,
                                          const std::string& name) {
  ArcAppItem* app_item = GetArcAppItem(app_id);
  if (!app_item) {
    VLOG(2) << "Could not update the name of ARC app(" << app_id
            << ") because it was not found.";
    return;
  }

  app_item->SetName(name);
}

void ArcAppModelBuilder::OnListItemMoved(size_t from_index,
                                         size_t to_index,
                                         app_list::AppListItem* item) {
  // On ChromeOS we expect that ArcAppModelBuilder is initialized with
  // AppListSyncableService and in this case this observer is not used.
  NOTREACHED();
}
