// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved

#include "browser/startup_vivaldi_browser.h"

#include <string>

#include "app/vivaldi_apptools.h"
#include "app/vivaldi_constants.h"
#include "base/command_line.h"
#include "browser/init_sparkle.h"
#include "browser/launch_update_notifier.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/extensions/extension_metrics.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "prefs/vivaldi_pref_names.h"

using extensions::Extension;

namespace vivaldi {

namespace {
// Start copied from chrome\browser\ui\startup\startup_browser_creator_impl.cc

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utility functions ----------------------------------------------------------

// TODO(koz): Consolidate this function and remove the special casing.
const Extension* GetPlatformApp(Profile* profile,
                                const std::string& extension_id) {
  const Extension* extension =
      extensions::ExtensionRegistry::Get(profile)->GetExtensionById(
          extension_id, extensions::ExtensionRegistry::EVERYTHING);
  return extension && extension->is_platform_app() ? extension : NULL;
}

void RecordCmdLineAppHistogram(extensions::Manifest::Type app_type) {
  extensions::RecordAppLaunchType(extension_misc::APP_LAUNCH_CMD_LINE_APP,
                                  app_type);
}
// End copied
}  // anonymous namespace

GURL GetVivaldiNewTabURL() {
  return GURL(vivaldi::kVivaldiNewTabURL);
}

bool IsAutoupdateEnabled(Profile* profile) {
  const PrefService* prefs = profile->GetPrefs();
  return prefs->GetBoolean(vivaldiprefs::kAutoUpdateEnabled);
}

bool LaunchVivaldi(const base::CommandLine& command_line,
                   const base::FilePath& cur_dir,
                   Profile* profile) {
  if (!(IsVivaldiRunning(command_line) && !IsDebuggingVivaldi(command_line)))
    return false;
  // Default is now launching the Vivaldi App.
  // TODO(sverrir@vivaldi.com): Remove
  const Extension* extension = GetPlatformApp(profile, vivaldi::kVivaldiAppId);
  // If |vivaldi::kVivaldiAppId| is a disabled platform app we handle it
  // specially here, otherwise it will be handled below.
  if (extension) {
    RecordCmdLineAppHistogram(extensions::Manifest::TYPE_PLATFORM_APP);
    InitializeSparkle(command_line, base::Bind(&IsAutoupdateEnabled, profile));
  }
  LaunchUpdateNotifier();

  AppLaunchParams params(profile, extension, extensions::LAUNCH_CONTAINER_NONE,
                         WindowOpenDisposition::NEW_WINDOW,
                         extensions::SOURCE_EXTENSIONS_PAGE);
  params.command_line = command_line;
  params.current_directory = cur_dir;

  ::OpenApplicationWithReenablePrompt(params);

  return true;
}

bool AddVivaldiNewPage(bool welcome_run_none, std::vector<GURL>* startup_urls) {
  if (!IsVivaldiRunning())
    return false;

  if (welcome_run_none) {
    // Don't open speed dial if we will open first run
    startup_urls->push_back(GetVivaldiNewTabURL());
  }

  return true;
}

}  // namespace vivaldi
