// Copyright (c) 2013 Vivaldi Technologies AS. All rights reserved

#include <string>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/win/registry.h"
#include "chrome/browser/shell_integration.h"

#include "importer/viv_importer_utils.h"

using base::PathService;

static const wchar_t* kOperaRegPath = L"Software\\Opera Software";
static const wchar_t* kOpera = L"Opera";
static const wchar_t* kOpera64bitFolder = L"Opera x64";

base::FilePath GetOperaInstallPathFromRegistry() {
  // Detects the path that Opera is installed in.
  base::string16 registry_path = kOperaRegPath;
  wchar_t buffer[MAX_PATH];
  DWORD buffer_length = sizeof(buffer);
  base::win::RegKey reg_key(HKEY_CURRENT_USER, registry_path.c_str(), KEY_READ);
  buffer_length = sizeof(buffer);
  LONG result =
      reg_key.ReadValue(L"Last Install Path", buffer, &buffer_length, NULL);

  return (result != ERROR_SUCCESS) ? base::FilePath() : base::FilePath(buffer);
}

base::FilePath GetProfileDir() {
  base::FilePath profile_dir;
  // The default location of the profile folder containing user data is
  // under the "Application Data" folder in Windows XP, Vista, and 7.
  if (!PathService::Get(base::DIR_APP_DATA, &profile_dir))
    return base::FilePath();

  // Tree is Opera/Opera (for 32 bit) and Opera/Opera x64 (64 bit).
  // Use 64 bit if found otherwise 32 bit.
  profile_dir = profile_dir.AppendASCII("Opera");

  if (base::PathExists(profile_dir.Append(kOpera64bitFolder)))
    return profile_dir.Append(kOpera64bitFolder);
  else if (base::PathExists(profile_dir.Append(kOpera)))
    return profile_dir.Append(kOpera);
  else
    return base::FilePath();
}
