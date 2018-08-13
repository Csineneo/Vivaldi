// Copyright 2015 Vivaldi Technologies

#include "prefs/vivaldi_browser_prefs.h"

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "prefs/vivaldi_pref_names.h"
#include "vivaldi/prefs/vivaldi_gen_prefs.h"

namespace vivaldi {

void RegisterOldPlatformPrefs(user_prefs::PrefRegistrySyncable* registry) {
}

void MigrateOldPlatformPrefs(PrefService* prefs) {
}

std::unique_ptr<base::Value> GetPlatformComputedDefault(
    const std::string& path) {
  return std::make_unique<base::Value>();
}

std::string GetPlatformDefaultKey() {
  return "default_android";
}

}  // namespace vivaldi
