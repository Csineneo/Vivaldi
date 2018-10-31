// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved

#include "extensions/browser/extension_prefs.h"

#include "app/vivaldi_constants.h"
#include "extensions/browser/extension_pref_value_map.h"

namespace extensions {

void ExtensionPrefs::RegisterAndLoadExtPrefsForVivaldi() {

  extension_pref_value_map_->RegisterExtension(vivaldi::kVivaldiAppId,
                          base::Time(),
                          true,
                          true);

  // Set regular extension controlled prefs.
  LoadExtensionControlledPrefs(vivaldi::kVivaldiAppId,
                               kExtensionPrefsScopeRegular);
  // Set incognito extension controlled prefs.
  LoadExtensionControlledPrefs(vivaldi::kVivaldiAppId,
                               kExtensionPrefsScopeIncognitoPersistent);
  // Set regular-only extension controlled prefs.
  LoadExtensionControlledPrefs(vivaldi::kVivaldiAppId,
                               kExtensionPrefsScopeRegularOnly);
}

}  // namespace extensions
