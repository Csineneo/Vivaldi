// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/utility/importer/importer_creator.h"

#include "base/logging.h"
#include "build/build_config.h"
#include "chrome/utility/importer/bookmarks_file_importer.h"
#include "chrome/utility/importer/firefox_importer.h"

#if defined(OS_WIN)
#include "chrome/common/importer/edge_importer_utils_win.h"
#include "chrome/utility/importer/edge_importer_win.h"
#include "chrome/utility/importer/ie_importer_win.h"
#endif

#if defined(OS_MACOSX)
#include <CoreFoundation/CoreFoundation.h>

#include "base/mac/foundation_util.h"
#include "chrome/utility/importer/safari_importer.h"
#endif

#include "importer/chromium_importer.h"
#include "importer/viv_importer.h"

namespace importer {

scoped_refptr<Importer> CreateImporterByType(ImporterType type,
       const ImportConfig &import_config) {
  switch (type) {
#if defined(OS_WIN)
    case TYPE_IE:
      return new IEImporter();
    case TYPE_EDGE:
      // If legacy mode we pass back an IE importer.
      if (IsEdgeFavoritesLegacyMode())
        return new IEImporter();
      return new EdgeImporter();
#endif
    case TYPE_BOOKMARKS_FILE:
      return new BookmarksFileImporter();
    case TYPE_FIREFOX:
      return new FirefoxImporter();
#if defined(OS_MACOSX)
    case TYPE_SAFARI:
      return new SafariImporter(base::mac::GetUserLibraryPath());
#endif
    case TYPE_OPERA:
      return new OperaImporter(import_config);
    case TYPE_OPERA_BOOKMARK_FILE:
      return new OperaImporter(import_config);
    case TYPE_CHROME:
    case TYPE_CHROMIUM:
    case TYPE_YANDEX:
    case TYPE_OPERA_OPIUM:
    case TYPE_OPERA_OPIUM_BETA:
    case TYPE_OPERA_OPIUM_DEV:
    case TYPE_VIVALDI:
      return new ChromiumImporter(import_config);
    default:
      NOTREACHED();
      return nullptr;
  }
}

}  // namespace importer
