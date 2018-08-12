// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included file, no traditional include guard.
#include <string>
#include <vector>

#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_platform_file.h"
#include "third_party/WebKit/public/platform/modules/permissions/permission_status.mojom.h"
#include "url/gurl.h"
#include "url/ipc/url_param_traits.h"

#define IPC_MESSAGE_START LayoutTestMsgStart

IPC_ENUM_TRAITS_MIN_MAX_VALUE(blink::mojom::PermissionStatus,
                              blink::mojom::PermissionStatus::GRANTED,
                              blink::mojom::PermissionStatus::ASK)

IPC_SYNC_MESSAGE_ROUTED1_1(LayoutTestHostMsg_ReadFileToString,
                           base::FilePath /* local path */,
                           std::string /* contents */)
IPC_SYNC_MESSAGE_ROUTED1_1(LayoutTestHostMsg_RegisterIsolatedFileSystem,
                           std::vector<base::FilePath> /* absolute_filenames */,
                           std::string /* filesystem_id */)
IPC_MESSAGE_ROUTED0(LayoutTestHostMsg_ClearAllDatabases)
IPC_MESSAGE_ROUTED1(LayoutTestHostMsg_SetDatabaseQuota,
                    int /* quota */)
IPC_MESSAGE_ROUTED2(LayoutTestHostMsg_SimulateWebNotificationClick,
                    std::string /* title */,
                    int /* action_index */)
IPC_MESSAGE_ROUTED2(LayoutTestHostMsg_SimulateWebNotificationClose,
                    std::string /* title */,
                    bool /* by_user */)
IPC_MESSAGE_ROUTED1(LayoutTestHostMsg_AcceptAllCookies,
                    bool /* accept */)
IPC_MESSAGE_ROUTED0(LayoutTestHostMsg_DeleteAllCookies)
IPC_MESSAGE_ROUTED4(LayoutTestHostMsg_SetPermission,
                    std::string /* name */,
                    blink::mojom::PermissionStatus /* status */,
                    GURL /* origin */,
                    GURL /* embedding_origin */)
IPC_MESSAGE_ROUTED0(LayoutTestHostMsg_ResetPermissions)
