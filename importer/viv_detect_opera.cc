// Copyright (c) 2013 Vivaldi Technologies AS. All rights reserved

#include "importer/viv_importer.h"

#include "app/vivaldi_resources.h"
#include "importer/viv_importer_utils.h"
#include "ui/base/l10n/l10n_util.h"

//static const char OPERA_PREFS_NAME[] = "operaprefs.ini";
//static const char OPERA_SPEEDDIAL_NAME[] = "speeddial.ini";

namespace viv_importer {

void DetectOperaProfiles(std::vector<importer::SourceProfile>* profiles) {
  importer::SourceProfile opera;
  opera.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_OPERA);
  opera.importer_type = importer::TYPE_OPERA;
  opera.source_path = GetProfileDir();
#if defined(OS_WIN)
  opera.app_path = GetOperaInstallPathFromRegistry();
#endif
  opera.services_supported = importer::SPEED_DIAL |
      importer::FAVORITES | importer::NOTES | importer::PASSWORDS /*|
          importer::HISTORY |
          importer::COOKIES |
          importer::SEARCH_ENGINES*/;

#if 0
  // Check if this profile need the master password
  DictionaryValueINIParser inifile_parser;
  base::FilePath ini_file(opera.source_path);
  ini_file = ini_file.AppendASCII(OPERA_PREFS_NAME);
  if (ReadOperaIniFile(ini_file, inifile_parser)) {
    const base::DictionaryValue& inifile = inifile_parser.root();
    int val;
    inifile.GetInteger("Security Prefs.Use Paranoid Mailpassword", &val);
    if (val) {
      opera.services_supported |= importer::MASTER_PASSWORD;
    }
  }
#else
  // NOTE(pettern):
  // If we import from a different profile, we can't check the default
  // profile prefs file. Disable it for now until we have a better solution.
  opera.services_supported |= importer::MASTER_PASSWORD;
#endif
  profiles->push_back(opera);
}
}  // namespace viv_importer
