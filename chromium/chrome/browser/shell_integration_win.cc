// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/shell_integration.h"

#include <windows.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <propkey.h>  // Needs to come after shobjidl.h.
#include <stddef.h>
#include <stdint.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/macros.h"
#include "base/md5.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#include "base/win/scoped_comptr.h"
#include "base/win/scoped_propvariant.h"
#include "base/win/shortcut.h"
#include "base/win/windows_version.h"
#include "chrome/browser/policy/policy_path_parser.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/setup/setup_util.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/create_reg_key_work_item.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/set_reg_value_work_item.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/installer/util/util_constants.h"
#include "chrome/installer/util/work_item.h"
#include "chrome/installer/util/work_item_list.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_thread.h"

#include "app/vivaldi_apptools.h"
#include "app/vivaldi_constants.h"
#include "browser/win/vivaldi_standalone.h"

using content::BrowserThread;

namespace shell_integration {

namespace {

const wchar_t kAppListAppNameSuffix[] = L"AppList";

// Helper function for GetAppId to generates profile id
// from profile path. "profile_id" is composed of sanitized basenames of
// user data dir and profile dir joined by a ".".
// For Vivaldi standalone, make a MD5 checksum of the profile path characters.
base::string16 GetProfileIdFromPath(const base::FilePath& profile_path) {
  // Return empty string if profile_path is empty
  if (profile_path.empty())
    return base::string16();

  base::string16 profile_id;

  bool is_standalone = vivaldi::IsStandalone();
  if (is_standalone) {
    base::MD5Digest md5_digest;
    std::string profile_path_ascii(base::UTF16ToASCII(profile_path.value()));
    base::MD5Sum(profile_path_ascii.c_str(), profile_path_ascii.length(),
        &md5_digest);
    profile_id = base::ASCIIToUTF16(base::MD5DigestToBase16(md5_digest));
  } else {
    base::FilePath default_user_data_dir;
    // Return empty string if profile_path is in default user data
    // dir and is the default profile.
    if (chrome::GetDefaultUserDataDirectory(&default_user_data_dir) &&
        profile_path.DirName() == default_user_data_dir &&
        profile_path.BaseName().value() ==
          base::ASCIIToUTF16(chrome::kInitialProfile)) {
      return base::string16();
    }
    // Get joined basenames of user data dir and profile.
    base::string16 basenames = profile_path.DirName().BaseName().value() + L"."
        + profile_path.BaseName().value();

    profile_id.reserve(basenames.size());

    // Generate profile_id from sanitized basenames.
    for (size_t i = 0; i < basenames.length(); ++i) {
      if (base::IsAsciiAlpha(basenames[i]) ||
          base::IsAsciiDigit(basenames[i]) ||
          basenames[i] == L'.') {
        profile_id += basenames[i];
      }
    }
  }

  return profile_id;
}

base::string16 GetAppListAppName() {
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  base::string16 app_name(dist->GetBaseAppId());
  app_name.append(kAppListAppNameSuffix);
  return app_name;
}

// Gets expected app id for given Chrome (based on |command_line| and
// |is_per_user_install|).
base::string16 GetExpectedAppId(const base::CommandLine& command_line,
                                bool is_per_user_install) {
  base::FilePath user_data_dir;
  if (command_line.HasSwitch(switches::kUserDataDir))
    user_data_dir = command_line.GetSwitchValuePath(switches::kUserDataDir);
  else
    chrome::GetDefaultUserDataDirectory(&user_data_dir);
  // Adjust with any policy that overrides any other way to set the path.
  policy::path_parser::CheckUserDataDirPolicy(&user_data_dir);
  DCHECK(!user_data_dir.empty());

  base::FilePath profile_subdir;
  if (command_line.HasSwitch(switches::kProfileDirectory)) {
    profile_subdir =
        command_line.GetSwitchValuePath(switches::kProfileDirectory);
  } else {
    profile_subdir =
        base::FilePath(base::ASCIIToUTF16(chrome::kInitialProfile));
  }
  DCHECK(!profile_subdir.empty());

  base::FilePath profile_path = user_data_dir.Append(profile_subdir);
  base::string16 app_name;
  if (command_line.HasSwitch(switches::kApp)) {
    app_name = base::UTF8ToUTF16(web_app::GenerateApplicationNameFromURL(
        GURL(command_line.GetSwitchValueASCII(switches::kApp))));
  } else if (command_line.HasSwitch(switches::kAppId)) {
    app_name = base::UTF8ToUTF16(
        web_app::GenerateApplicationNameFromExtensionId(
            command_line.GetSwitchValueASCII(switches::kAppId)));
  } else if (command_line.HasSwitch(switches::kShowAppList)) {
    app_name = GetAppListAppName();
  } else if (vivaldi::IsVivaldiRunning()) {
    // Note(yngve) Check actual application commandline, not the possibly
    // generated commanline argument, as vivaldi is by default running
    app_name = base::UTF8ToUTF16(
      web_app::GenerateApplicationNameFromExtensionId(vivaldi::kVivaldiAppId));
  } else {
    BrowserDistribution* dist = BrowserDistribution::GetDistribution();
    app_name = ShellUtil::GetBrowserModelId(dist, is_per_user_install);
  }
  DCHECK(!app_name.empty());

  return GetAppModelIdForProfile(app_name, profile_path);
}

void MigrateTaskbarPinsCallback() {
  // This should run on the file thread.
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);

  // Get full path of chrome.
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe))
    return;

  base::FilePath pins_path;
  if (!PathService::Get(base::DIR_TASKBAR_PINS, &pins_path)) {
    NOTREACHED();
    return;
  }

  MigrateShortcutsInPathInternal(chrome_exe, pins_path);
}

// Windows 8 introduced a new protocol->executable binding system which cannot
// be retrieved in the HKCR registry subkey method implemented below. We call
// AssocQueryString with the new Win8-only flag ASSOCF_IS_PROTOCOL instead.
base::string16 GetAppForProtocolUsingAssocQuery(const GURL& url) {
  base::string16 url_scheme = base::ASCIIToUTF16(url.scheme());
  // Don't attempt to query protocol association on an empty string.
  if (url_scheme.empty())
    return base::string16();

  // Query AssocQueryString for a human-readable description of the program
  // that will be invoked given the provided URL spec. This is used only to
  // populate the external protocol dialog box the user sees when invoking
  // an unknown external protocol.
  wchar_t out_buffer[1024];
  DWORD buffer_size = arraysize(out_buffer);
  HRESULT hr = AssocQueryString(ASSOCF_IS_PROTOCOL,
                                ASSOCSTR_FRIENDLYAPPNAME,
                                url_scheme.c_str(),
                                NULL,
                                out_buffer,
                                &buffer_size);
  if (FAILED(hr)) {
    DLOG(WARNING) << "AssocQueryString failed!";
    return base::string16();
  }
  return base::string16(out_buffer);
}

base::string16 GetAppForProtocolUsingRegistry(const GURL& url) {
  const base::string16 cmd_key_path =
      base::ASCIIToUTF16(url.scheme() + "\\shell\\open\\command");
  base::win::RegKey cmd_key(HKEY_CLASSES_ROOT,
                            cmd_key_path.c_str(),
                            KEY_READ);
  base::string16 application_to_launch;
  if (cmd_key.ReadValue(NULL, &application_to_launch) == ERROR_SUCCESS) {
    const base::string16 url_spec =
        base::ASCIIToUTF16(url.possibly_invalid_spec());
    base::ReplaceSubstringsAfterOffset(&application_to_launch,
                                       0,
                                       L"%1",
                                       url_spec);
    return application_to_launch;
  }
  return base::string16();
}

DefaultWebClientState GetDefaultWebClientStateFromShellUtilDefaultState(
    ShellUtil::DefaultState default_state) {
  switch (default_state) {
    case ShellUtil::NOT_DEFAULT:
      return DefaultWebClientState::NOT_DEFAULT;
    case ShellUtil::IS_DEFAULT:
      return DefaultWebClientState::IS_DEFAULT;
    default:
      DCHECK_EQ(ShellUtil::UNKNOWN_DEFAULT, default_state);
      return DefaultWebClientState::UNKNOWN_DEFAULT;
  }
}

}  // namespace

bool SetAsDefaultBrowser() {
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return false;
  }

  // From UI currently we only allow setting default browser for current user.
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  if (!ShellUtil::MakeChromeDefault(dist, ShellUtil::CURRENT_USER, chrome_exe,
                                    true /* elevate_if_not_admin */)) {
    LOG(ERROR) << "Chrome could not be set as default browser.";
    return false;
  }

  VLOG(1) << "Chrome registered as default browser.";
  return true;
}

bool SetAsDefaultBrowserInteractive() {
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    return false;
  }

  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  if (!ShellUtil::ShowMakeChromeDefaultSystemUI(dist, chrome_exe)) {
    LOG(ERROR) << "Failed to launch the set-default-browser Windows UI.";
    return false;
  }

  VLOG(1) << "Set-default-browser Windows UI completed.";
  return true;
}

bool SetAsDefaultProtocolClient(const std::string& protocol) {
  if (protocol.empty())
    return false;

  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return false;
  }

  base::string16 wprotocol(base::UTF8ToUTF16(protocol));
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  if (!ShellUtil::MakeChromeDefaultProtocolClient(dist, chrome_exe,
                                                  wprotocol)) {
    LOG(ERROR) << "Chrome could not be set as default handler for "
               << protocol << ".";
    return false;
  }

  VLOG(1) << "Chrome registered as default handler for " << protocol << ".";
  return true;
}

bool SetAsDefaultProtocolClientInteractive(const std::string& protocol) {
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    return false;
  }

  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  base::string16 wprotocol(base::UTF8ToUTF16(protocol));
  if (!ShellUtil::ShowMakeChromeDefaultProtocolClientSystemUI(dist, chrome_exe,
                                                              wprotocol)) {
    LOG(ERROR) << "Failed to launch the set-default-client Windows UI.";
    return false;
  }

  VLOG(1) << "Set-default-client Windows UI completed.";
  return true;
}

DefaultWebClientSetPermission CanSetAsDefaultBrowser() {
  BrowserDistribution* distribution = BrowserDistribution::GetDistribution();
  if (distribution->GetDefaultBrowserControlPolicy() !=
          BrowserDistribution::DEFAULT_BROWSER_FULL_CONTROL)
    return SET_DEFAULT_NOT_ALLOWED;
  if (ShellUtil::CanMakeChromeDefaultUnattended())
    return SET_DEFAULT_UNATTENDED;
  return SET_DEFAULT_INTERACTIVE;
}

bool IsElevationNeededForSettingDefaultProtocolClient() {
  return base::win::GetVersion() < base::win::VERSION_WIN8;
}

base::string16 GetApplicationNameForProtocol(const GURL& url) {
  // Windows 8 or above requires a new protocol association query.
  if (base::win::GetVersion() >= base::win::VERSION_WIN8)
    return GetAppForProtocolUsingAssocQuery(url);
  else
    return GetAppForProtocolUsingRegistry(url);
}

DefaultWebClientState GetDefaultBrowser() {
  return GetDefaultWebClientStateFromShellUtilDefaultState(
      ShellUtil::GetChromeDefaultState());
}

// There is no reliable way to say which browser is default on a machine (each
// browser can have some of the protocols/shortcuts). So we look for only HTTP
// protocol handler. Even this handler is located at different places in
// registry on XP and Vista:
// - HKCR\http\shell\open\command (XP)
// - HKCU\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\
//   http\UserChoice (Vista)
// This method checks if Firefox is defualt browser by checking these
// locations and returns true if Firefox traces are found there. In case of
// error (or if Firefox is not found)it returns the default value which
// is false.
bool IsFirefoxDefaultBrowser() {
  bool ff_default = false;
  if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
    base::string16 app_cmd;
    base::win::RegKey key(HKEY_CURRENT_USER,
                          ShellUtil::kRegVistaUrlPrefs, KEY_READ);
    if (key.Valid() && (key.ReadValue(L"Progid", &app_cmd) == ERROR_SUCCESS) &&
        app_cmd == L"FirefoxURL")
      ff_default = true;
  } else {
    base::string16 key_path(L"http");
    key_path.append(ShellUtil::kRegShellOpen);
    base::win::RegKey key(HKEY_CLASSES_ROOT, key_path.c_str(), KEY_READ);
    base::string16 app_cmd;
    if (key.Valid() && (key.ReadValue(L"", &app_cmd) == ERROR_SUCCESS) &&
        base::string16::npos !=
        base::ToLowerASCII(app_cmd).find(L"firefox"))
      ff_default = true;
  }
  return ff_default;
}

// There is no reliable way to say which browser is default on a machine (each
// browser can have some of the protocols/shortcuts). So we look for only HTTP
// protocol handler. Even this handler is located at different places in
// registry on XP and Vista:
// - HKCR\http\shell\open\command (XP)
// - HKCU\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\
//   http\UserChoice (Vista)
// This method checks if Chrome is default browser by checking these
// locations and returns true if Chrome traces are found there. In case of
// error (or if Chrome is not found) it returns the default value which
// is false.
bool IsChromeDefaultBrowser() {
  bool chrome_default = false;
  if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
    base::string16 app_cmd;
    base::win::RegKey key(HKEY_CURRENT_USER,
      ShellUtil::kRegVistaUrlPrefs, KEY_READ);
    if (key.Valid() && (key.ReadValue(L"Progid", &app_cmd) == ERROR_SUCCESS) &&
      app_cmd == L"chrome")
      chrome_default = true;
  } else {
    base::string16 key_path(L"http");
    key_path.append(ShellUtil::kRegShellOpen);
    base::win::RegKey key(HKEY_CLASSES_ROOT, key_path.c_str(), KEY_READ);
    base::string16 app_cmd;
    if (key.Valid() && (key.ReadValue(L"", &app_cmd) == ERROR_SUCCESS) &&
      base::string16::npos !=
      base::ToLowerASCII(app_cmd).find(L"chrome"))
      chrome_default = true;
  }
  return chrome_default;
}

// There is no reliable way to say which browser is default on a machine (each
// browser can have some of the protocols/shortcuts). So we look for only HTTP
// protocol handler. Even this handler is located at different places in
// registry on XP and Vista:
// - HKCR\http\shell\open\command (XP)
// - HKCU\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\
//   http\UserChoice (Vista)
// This method checks if Opera is defualt browser by checking these
// locations and returns true if Firefox traces are found there. In case of
// error (or if Opera is not found)it returns the default value which
// is false.
bool IsOperaDefaultBrowser() {
  bool opera_default = false;
  if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
    base::string16 app_cmd;
    base::win::RegKey key(HKEY_CURRENT_USER,
                          ShellUtil::kRegVistaUrlPrefs, KEY_READ);
    if (key.Valid() && (key.ReadValue(L"Progid", &app_cmd) == ERROR_SUCCESS) &&
        base::string16::npos != base::ToLowerASCII(app_cmd).find(L"opera"))
      opera_default = true;
  } else {
    base::string16 key_path(L"http");
    key_path.append(ShellUtil::kRegShellOpen);
    base::win::RegKey key(HKEY_CLASSES_ROOT, key_path.c_str(), KEY_READ);
    base::string16 app_cmd;
    if (key.Valid() && (key.ReadValue(L"", &app_cmd) == ERROR_SUCCESS) &&
        base::string16::npos != base::ToLowerASCII(app_cmd).find(L"opera"))
      opera_default = true;
  }
  return opera_default;
}


DefaultWebClientState IsDefaultProtocolClient(const std::string& protocol) {
  return GetDefaultWebClientStateFromShellUtilDefaultState(
      ShellUtil::GetChromeDefaultProtocolClientState(
          base::UTF8ToUTF16(protocol)));
}

base::string16 GetAppModelIdForProfile(const base::string16& app_name,
                                       const base::FilePath& profile_path) {
  std::vector<base::string16> components;
  components.push_back(app_name);
  const base::string16 profile_id(GetProfileIdFromPath(profile_path));
  if (!profile_id.empty())
    components.push_back(profile_id);
  return ShellUtil::BuildAppModelId(components);
}

base::string16 GetChromiumModelIdForProfile(
    const base::FilePath& profile_path) {
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
    return dist->GetBaseAppId();
  }
  return GetAppModelIdForProfile(
      ShellUtil::GetBrowserModelId(dist,
                                   InstallUtil::IsPerUserInstall(chrome_exe)),
      profile_path);
}

base::string16 GetAppListAppModelIdForProfile(
    const base::FilePath& profile_path) {
  return GetAppModelIdForProfile(GetAppListAppName(), profile_path);
}

void MigrateTaskbarPins() {
  if (base::win::GetVersion() < base::win::VERSION_WIN7)
    return;

  // This needs to happen eventually (e.g. so that the appid is fixed and the
  // run-time Chrome icon is merged with the taskbar shortcut), but this is not
  // urgent and shouldn't delay Chrome startup.
  static const int64_t kMigrateTaskbarPinsDelaySeconds = 15;
  BrowserThread::PostDelayedTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&MigrateTaskbarPinsCallback),
      base::TimeDelta::FromSeconds(kMigrateTaskbarPinsDelaySeconds));
}

int MigrateShortcutsInPathInternal(const base::FilePath& chrome_exe,
                                   const base::FilePath& path) {
  DCHECK(base::win::GetVersion() >= base::win::VERSION_WIN7);

  // Enumerate all pinned shortcuts in the given path directly.
  base::FileEnumerator shortcuts_enum(
      path, false,  // not recursive
      base::FileEnumerator::FILES, FILE_PATH_LITERAL("*.lnk"));

  bool is_per_user_install = InstallUtil::IsPerUserInstall(chrome_exe);

  int shortcuts_migrated = 0;
  base::FilePath target_path;
  base::string16 arguments;
  base::win::ScopedPropVariant propvariant;
  for (base::FilePath shortcut = shortcuts_enum.Next(); !shortcut.empty();
       shortcut = shortcuts_enum.Next()) {
    // TODO(gab): Use ProgramCompare instead of comparing FilePaths below once
    // it is fixed to work with FilePaths with spaces.
    if (!base::win::ResolveShortcut(shortcut, &target_path, &arguments) ||
        chrome_exe != target_path) {
      continue;
    }
    base::CommandLine command_line(
        base::CommandLine::FromString(base::StringPrintf(
            L"\"%ls\" %ls", target_path.value().c_str(), arguments.c_str())));

    // Get the expected AppId for this Chrome shortcut.
    base::string16 expected_app_id(
        GetExpectedAppId(command_line, is_per_user_install));
    if (expected_app_id.empty())
      continue;

    // Load the shortcut.
    base::win::ScopedComPtr<IShellLink> shell_link;
    base::win::ScopedComPtr<IPersistFile> persist_file;
    if (FAILED(shell_link.CreateInstance(CLSID_ShellLink, NULL,
                                         CLSCTX_INPROC_SERVER)) ||
        FAILED(persist_file.QueryFrom(shell_link.get())) ||
        FAILED(persist_file->Load(shortcut.value().c_str(), STGM_READ))) {
      DLOG(WARNING) << "Failed loading shortcut at " << shortcut.value();
      continue;
    }

    // Any properties that need to be updated on the shortcut will be stored in
    // |updated_properties|.
    base::win::ShortcutProperties updated_properties;

    // Validate the existing app id for the shortcut.
    base::win::ScopedComPtr<IPropertyStore> property_store;
    propvariant.Reset();
    if (FAILED(property_store.QueryFrom(shell_link.get())) ||
        property_store->GetValue(PKEY_AppUserModel_ID, propvariant.Receive()) !=
            S_OK) {
      // When in doubt, prefer not updating the shortcut.
      NOTREACHED();
      continue;
    } else {
      switch (propvariant.get().vt) {
        case VT_EMPTY:
          // If there is no app_id set, set our app_id if one is expected.
          if (!expected_app_id.empty())
            updated_properties.set_app_id(expected_app_id);
          break;
        case VT_LPWSTR:
          if (expected_app_id != base::string16(propvariant.get().pwszVal))
            updated_properties.set_app_id(expected_app_id);
          break;
        default:
          NOTREACHED();
          continue;
      }
    }

    // Clear dual_mode property from any shortcuts that previously had it (it
    // was only ever installed on shortcuts with the
    // |default_chromium_model_id|).
    BrowserDistribution* dist = BrowserDistribution::GetDistribution();
    base::string16 default_chromium_model_id(
        ShellUtil::GetBrowserModelId(dist, is_per_user_install));
    if (expected_app_id == default_chromium_model_id) {
      propvariant.Reset();
      if (property_store->GetValue(PKEY_AppUserModel_IsDualMode,
                                   propvariant.Receive()) != S_OK) {
        // When in doubt, prefer to not update the shortcut.
        NOTREACHED();
        continue;
      }
      if (propvariant.get().vt == VT_BOOL &&
                 !!propvariant.get().boolVal) {
        updated_properties.set_dual_mode(false);
      }
    }

    persist_file.Release();
    shell_link.Release();

    // Update the shortcut if some of its properties need to be updated.
    if (updated_properties.options &&
        base::win::CreateOrUpdateShortcutLink(
            shortcut, updated_properties,
            base::win::SHORTCUT_UPDATE_EXISTING)) {
      ++shortcuts_migrated;
    }
  }
  return shortcuts_migrated;
}

base::FilePath GetStartMenuShortcut(const base::FilePath& chrome_exe) {
  static const int kFolderIds[] = {
    base::DIR_COMMON_START_MENU,
    base::DIR_START_MENU,
  };
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  const base::string16 shortcut_name(
      dist->GetShortcutName(BrowserDistribution::SHORTCUT_CHROME) +
      installer::kLnkExt);
  base::FilePath programs_folder;
  base::FilePath shortcut;

  // Check both the common and the per-user Start Menu folders for system-level
  // installs.
  size_t folder = InstallUtil::IsPerUserInstall(chrome_exe) ? 1 : 0;
  for (; folder < arraysize(kFolderIds); ++folder) {
    if (!PathService::Get(kFolderIds[folder], &programs_folder)) {
      NOTREACHED();
      continue;
    }

    shortcut = programs_folder.Append(shortcut_name);
    if (base::PathExists(shortcut))
      return shortcut;
  }

  return base::FilePath();
}

}  // namespace shell_integration
