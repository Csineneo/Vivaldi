// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Singly or Multiply-included shared traits file depending on circumstances.
// This allows the use of IPC serialization macros in more than one IPC message
// file.
#ifndef VIVALDI_IMPORTER_PROFILE_IMPORT_PROCESS_PARAM_TRAITS_MACROS_H_
#define VIVALDI_IMPORTER_PROFILE_IMPORT_PROCESS_PARAM_TRAITS_MACROS_H_

#include <string>
#include <vector>

#include "base/strings/string16.h"
#include "base/values.h"
#include "chrome/common/common_param_traits_macros.h"
#include "chrome/common/importer/importer_data_types.h"
#include "content/public/common/common_param_traits.h"
#include "ipc/ipc_message_macros.h"

#include "importer/imported_notes_entry.h"
#include "importer/imported_speeddial_entry.h"
#include "importer/viv_importer.h"

IPC_STRUCT_TRAITS_BEGIN(ImportedNotesEntry)
  IPC_STRUCT_TRAITS_MEMBER(is_folder)
  IPC_STRUCT_TRAITS_MEMBER(url)
  IPC_STRUCT_TRAITS_MEMBER(path)
  IPC_STRUCT_TRAITS_MEMBER(title)
  IPC_STRUCT_TRAITS_MEMBER(content)
  IPC_STRUCT_TRAITS_MEMBER(creation_time)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(ImportedSpeedDialEntry)
  IPC_STRUCT_TRAITS_MEMBER(title)
  IPC_STRUCT_TRAITS_MEMBER(url)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(importer::ChromeProfileInfo)
  IPC_STRUCT_TRAITS_MEMBER(profileName)
  IPC_STRUCT_TRAITS_MEMBER(profileDisplayName)
IPC_STRUCT_TRAITS_END()

#endif  // VIVALDI_IMPORTER_PROFILE_IMPORT_PROCESS_PARAM_TRAITS_MACROS_H_
