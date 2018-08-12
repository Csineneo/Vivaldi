// Copyright (c) 2013-2016 Vivaldi Technologies AS. All rights reserved


#include <stack>
#include <string>

#include "importer/viv_opera_reader.h"

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/common/ini_parser.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_tokenizer.h"
#include "base/path_service.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/common/importer/imported_bookmark_entry.h"
#include "chrome/common/importer/importer_bridge.h"
#include "chrome/common/importer/importer_data_types.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

OperaAdrFileReader::OperaAdrFileReader() {}

OperaAdrFileReader::~OperaAdrFileReader() {}

bool OperaAdrFileReader::LoadFile(base::FilePath &file) {
  if (!base::PathExists(file)) {
    return false;
  }
  std::string bookmark_data;
  base::ReadFileToString(file, &bookmark_data);

  base::StringTokenizer tokenizer(bookmark_data, "\r\n");

  std::string category;
  base::DictionaryValue entries;
  while (tokenizer.GetNext()) {
    std::string line = tokenizer.token();

    base::TrimWhitespaceASCII(line, base::TRIM_ALL, &line);
    if (line.empty()) continue;

    if (line[0] == '-' || line[0] == '#') {
      if (!category.empty()) HandleEntry(category, entries);
      entries.Clear();
      if (line[0] == '-') {
        HandleEntry("-", entries);
        category = "";
        continue;
      }

      // #foo
      category = base::ToLowerASCII(line.substr(1));  // Strip away leading'#'
      continue;
    }
    size_t equal = line.find_first_of('=');
    if (equal != std::string::npos) {
      std::string key = base::ToLowerASCII(line.substr(0, equal));
      std::string val = line.substr(equal + 1);
      entries.SetString(key, val);
    }
  }
  if (entries.size() > 0) {
    HandleEntry(category, entries);
  }
  return true;
}
