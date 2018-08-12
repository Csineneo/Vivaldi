// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/build_settings.h"

#include <utility>

#include "base/files/file_util.h"
#include "tools/gn/filesystem_utils.h"

// <Vivaldi>
std::vector<BuildSettings::path_mapper> BuildSettings::path_map_;
// </Vivaldi>

BuildSettings::BuildSettings() {
}

BuildSettings::BuildSettings(const BuildSettings& other)
    : root_path_(other.root_path_),
      root_path_utf8_(other.root_path_utf8_),
      secondary_source_path_(other.secondary_source_path_),
      python_path_(other.python_path_),
      build_config_file_(other.build_config_file_),
      build_dir_(other.build_dir_),
      build_args_(other.build_args_) {
}

BuildSettings::~BuildSettings() {
}

void BuildSettings::SetRootPath(const base::FilePath& r) {
  DCHECK(r.value()[r.value().size() - 1] != base::FilePath::kSeparators[0]);
  root_path_ = r.NormalizePathSeparatorsTo('/');
  root_path_utf8_ = FilePathToUTF8(root_path_);
}

void BuildSettings::SetSecondarySourcePath(const SourceDir& d) {
  secondary_source_path_ = GetFullPath(d).NormalizePathSeparatorsTo('/');
}

void BuildSettings::SetBuildDir(const SourceDir& d) {
  build_dir_ = d;
}

base::FilePath BuildSettings::GetFullPath(const SourceFile& file) const {
  return file.Resolve(root_path_, true).NormalizePathSeparatorsTo('/');
}

base::FilePath BuildSettings::GetFullPath(const SourceDir& dir) const {
  return dir.Resolve(root_path_, true).NormalizePathSeparatorsTo('/');
}

base::FilePath BuildSettings::GetFullPathSecondary(
    const SourceFile& file) const {
  return file.Resolve(secondary_source_path_, false).NormalizePathSeparatorsTo('/');
}

base::FilePath BuildSettings::GetFullPathSecondary(
    const SourceDir& dir) const {
  return dir.Resolve(secondary_source_path_, false).NormalizePathSeparatorsTo('/');
}

void BuildSettings::ItemDefined(std::unique_ptr<Item> item) const {
  DCHECK(item);
  if (!item_defined_callback_.is_null())
    item_defined_callback_.Run(std::move(item));
}

// <Vivaldi>
bool BuildSettings::RegisterPathMap(const std::string &prefix,
                       const std::string &map_to_path)
{
  if (prefix.length()<2 || prefix[0] != '/' || prefix[1] != '/')
    return false;

  if (IsPathAbsolute(map_to_path))
    return false;

  path_mapper map_entry;

  map_entry.prefix.assign(prefix, 2, std::string::npos);
  if (EndsWithSlash(map_entry.prefix))
    map_entry.prefix.erase(map_entry.actual_path.length()-1);

  map_entry.actual_path.assign(map_to_path, 2, std::string::npos);
  if (EndsWithSlash(map_entry.actual_path))
    map_entry.actual_path.erase(map_entry.actual_path.length()-1);

  path_map_.push_back(map_entry);
  return true;
}

std::string BuildSettings::RemapSourcePathToActual(const std::string &path) {
  if (path.length()<2 || path[0] != '/' || path[1] != '/')
    return path;

  for (auto &&it: path_map_) {
    if (it.prefix.empty() ||
        (path.compare(2, it.prefix.length(), it.prefix) == 0 &&
        (2+it.prefix.length() == path.length() ||
          path[2+it.prefix.length()] == '/'))) {
      std::string new_path(path);

      if (!it.actual_path.empty() &&
          (new_path.compare(2, it.actual_path.length(), it.actual_path) != 0  ||
          (2+it.actual_path.length() > new_path.length() &&
            new_path[2+it.actual_path.length()] != '/'))) {
        new_path.insert(2, "/");
        new_path.insert(2, it.actual_path);
      } else if (it.actual_path.empty() &&
          new_path.compare(2, it.prefix.length(), it.prefix) == 0 &&
          (2+it.prefix.length()== new_path.length() ||
            new_path[2+it.prefix.length()] == '/')) {
        new_path.erase(2, it.prefix.length() +1);
      }
      return new_path;
    }
  }

  return path;
}

std::string BuildSettings::RemapActualToSourcePath(const std::string &path) {
  if (path.length()<2 || path[0] != '/' || path[1] != '/')
    return path;

  for (auto it= path_map_.rbegin(); it != path_map_.rend(); ++it) {
    if (it->actual_path.empty() ||
        (path.compare(2, it->actual_path.length(), it->actual_path) == 0 &&
        path[2+it->actual_path.length()] == '/')) {
      std::string new_path(path);

      if (!it->actual_path.empty() || (
          new_path.compare(2, it->prefix.length(), it->prefix) != 0  &&
          new_path[2+it->prefix.length()] == '/')) {
        new_path.replace(2, it->actual_path.length(), it->prefix);
      } else if (it->actual_path.empty() && !(
          new_path.compare(2, it->prefix.length(), it->prefix) == 0  &&
          new_path[2+it->prefix.length()] == '/')) {
        new_path.insert(2, "/");
        new_path.insert(2, it->prefix);
      }
      if (new_path[2] == '/')
        new_path.erase(2,1);
      return new_path;
    }
  }

  return path;
}
// </Vivaldi>
