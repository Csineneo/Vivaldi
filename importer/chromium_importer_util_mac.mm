// Copyright (c) 2013 Vivaldi Technologies AS. All rights reserved


#include <stack>
#include <string>

#include "chrome/browser/importer/importer_list.h"

#include "base/files/file_util.h"
#include "base/mac/foundation_util.h"
#include "base/mac/scoped_nsautorelease_pool.h"
#include "base/path_service.h"
#include "chrome/common/importer/importer_data_types.h"
#include "chrome/common/importer/importer_type.h"
#include "importer/chromium_profile_importer.h"
#include "importer/viv_importer_utils.h"

using base::PathService;

base::FilePath GetProfileDir(importer::ImporterType importerType){
  base::FilePath profile_path;
  base::FilePath app_data_path;
  if (!PathService::Get(base::DIR_APP_DATA, &app_data_path)) {
    return app_data_path.Append("not-supported");
  }

  switch (importerType) {
    case importer::TYPE_CHROME:
      profile_path = app_data_path.Append("Google").Append("Chrome");
      break;
    case importer::TYPE_VIVALDI:
      profile_path = app_data_path.Append("Vivaldi");
      break;
    case importer::TYPE_YANDEX:
      profile_path = app_data_path.Append("Yandex").Append("YandexBrowser");
      break;
    case importer::TYPE_OPERA_OPIUM:
      profile_path = app_data_path.Append("com.operasoftware.Opera");
      break;

    default:
      profile_path = app_data_path.Append("not-supported");
      break;
  }
  return profile_path;
}
