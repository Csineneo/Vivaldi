// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved

#include "extensions/permissions/vivaldi_api_permissions.h"

#include "base/memory/ptr_util.h"
#include "extensions/common/permissions/api_permission.h"

namespace extensions {

std::vector<std::unique_ptr<APIPermissionInfo>>
VivaldiAPIPermissions::GetAllPermissions() const {
  // WARNING: If you are modifying a permission message in this list, be sure to
  // add the corresponding permission message rule to
  // ChromePermissionMessageProvider::GetCoalescedPermissionMessages as well.
  APIPermissionInfo::InitInfo permissions_to_register[] = {
      // Register permissions for all extension types.
      {APIPermission::kAutoUpdate, "autoUpdate"},
      {APIPermission::kBookmarksPrivate, "bookmarksPrivate"},
      {APIPermission::kEditCommand, "editcommand" },
      {APIPermission::kExtensionActionUtils, "extensionActionUtils"},
      {APIPermission::kHistoryPrivate, "historyPrivate" },
      {APIPermission::kImportData, "importData"},
      {APIPermission::kNotes, "notes"},
      {APIPermission::kRuntimePrivate, "runtimePrivate" },
      {APIPermission::kSessionsPrivate, "sessionsPrivate" },
      {APIPermission::kSettings, "settings" },
      {APIPermission::kSavedPasswords, "savedpasswords"},
      {APIPermission::kShowMenu, "showMenu"},
      {APIPermission::kSync, "sync" },
      {APIPermission::kTabsPrivate, "tabsPrivate"},
      {APIPermission::kThumbnails, "thumbnails"},
      {APIPermission::kZoom, "zoom" },
      {APIPermission::kUtilities, "utilities"},
      {APIPermission::kWebViewPrivate, "webViewPrivate"},
  };

  std::vector<std::unique_ptr<APIPermissionInfo>> permissions;

  for (size_t i = 0; i < arraysize(permissions_to_register); ++i)
    permissions.push_back(
        base::WrapUnique(new APIPermissionInfo(permissions_to_register[i])));
  return permissions;
}

}   // namespace extensions
