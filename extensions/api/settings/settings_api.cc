// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved

#include "extensions/api/settings/settings_api.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_writer.h"
#include "base/macros.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/extension_web_ui.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/common/safe_browsing_prefs.h"
#include "content/public/browser/web_ui.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/schema/settings.h"
#include "prefs/vivaldi_browser_prefs.h"
#include "prefs/vivaldi_pref_names.h"

namespace extensions {

SettingsSetContentSettingFunction::SettingsSetContentSettingFunction() {}

SettingsSetContentSettingFunction::~SettingsSetContentSettingFunction() {}

ContentSetting ConvertToContentSetting(
    vivaldi::settings::ContentSettingEnum setting) {
  switch (setting) {
    case vivaldi::settings::CONTENT_SETTING_ENUM_ALLOW:
      return CONTENT_SETTING_ALLOW;
    case vivaldi::settings::CONTENT_SETTING_ENUM_BLOCK:
      return CONTENT_SETTING_BLOCK;
    case vivaldi::settings::CONTENT_SETTING_ENUM_ASK:
      return CONTENT_SETTING_ASK;
    case vivaldi::settings::CONTENT_SETTING_ENUM_SESSION_ONLY:
      return CONTENT_SETTING_SESSION_ONLY;
    case vivaldi::settings::CONTENT_SETTING_ENUM_DETECT_IMPORTANT_CONTENT:
      return CONTENT_SETTING_DETECT_IMPORTANT_CONTENT;
    default: {
      NOTREACHED();
      break;
    }
  }
  return CONTENT_SETTING_DEFAULT;
}

ContentSettingsType ConvertToContentSettingsType(
    vivaldi::settings::ContentSettingsTypeEnum type) {
  switch (type) {
    case vivaldi::settings::CONTENT_SETTINGS_TYPE_ENUM_PLUGINS:
      return CONTENT_SETTINGS_TYPE_PLUGINS;
    case vivaldi::settings::CONTENT_SETTINGS_TYPE_ENUM_POPUPS:
      return CONTENT_SETTINGS_TYPE_POPUPS;
    case vivaldi::settings::CONTENT_SETTINGS_TYPE_ENUM_GEOLOCATION:
      return CONTENT_SETTINGS_TYPE_GEOLOCATION;
    case vivaldi::settings::CONTENT_SETTINGS_TYPE_ENUM_NOTIFICATIONS:
      return CONTENT_SETTINGS_TYPE_NOTIFICATIONS;
    default: {
      NOTREACHED();
      break;
    }
  }
  return CONTENT_SETTINGS_TYPE_DEFAULT;
}

bool SettingsSetContentSettingFunction::RunAsync() {
  std::unique_ptr<vivaldi::settings::SetContentSetting::Params> params(
      vivaldi::settings::SetContentSetting::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  HostContentSettingsMap* content_settings =
      HostContentSettingsMapFactory::GetForProfile(GetProfile());

  const GURL primary_pattern(params->settings_item.primary_pattern);
  const GURL secondary_pattern(params->settings_item.secondary_pattern);

  ContentSettingsType type =
      ConvertToContentSettingsType(params->settings_item.type);
  ContentSetting setting =
      ConvertToContentSetting(params->settings_item.setting);

  content_settings->SetNarrowestContentSetting(
      primary_pattern, secondary_pattern, type, setting);

  SendResponse(true);
  return true;
}

}  // namespace extensions
