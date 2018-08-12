// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/named_platform_handle_utils.h"

#include <sddl.h>
#include <windows.h>

#include <memory>

#include "base/logging.h"
#include "base/win/windows_version.h"
#include "mojo/edk/embedder/named_platform_handle.h"

namespace mojo {
namespace edk {

ScopedPlatformHandle CreateClientHandle(
    const NamedPlatformHandle& named_handle) {
  if (!named_handle.is_valid())
    return ScopedPlatformHandle();

  base::string16 pipe_name = named_handle.pipe_name();

  // Note: This may block.
  if (!WaitNamedPipeW(pipe_name.c_str(), NMPWAIT_USE_DEFAULT_WAIT))
    return ScopedPlatformHandle();

  const DWORD kDesiredAccess = GENERIC_READ | GENERIC_WRITE;
  // The SECURITY_ANONYMOUS flag means that the server side cannot impersonate
  // the client.
  const DWORD kFlags =
      SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS | FILE_FLAG_OVERLAPPED;
  ScopedPlatformHandle handle(
      PlatformHandle(CreateFileW(pipe_name.c_str(), kDesiredAccess,
                                 0,  // No sharing.
                                 nullptr, OPEN_EXISTING, kFlags,
                                 nullptr)));  // No template file.
  PCHECK(handle.is_valid());
  return handle;
}

ScopedPlatformHandle CreateServerHandle(const NamedPlatformHandle& named_handle,
                                        bool enforce_uniqueness) {
  if (!named_handle.is_valid())
    return ScopedPlatformHandle();

  PSECURITY_DESCRIPTOR security_desc = nullptr;
  ULONG security_desc_len = 0;
  // Create a DACL to grant:
  // GA = Generic All
  // access to:
  // SY = LOCAL_SYSTEM
  // BA = BUILTIN_ADMINISTRATORS
  // OW = OWNER_RIGHTS
  PCHECK(ConvertStringSecurityDescriptorToSecurityDescriptor(
      L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)", SDDL_REVISION_1,
      &security_desc, &security_desc_len));
  std::unique_ptr<void, decltype(::LocalFree)*> p(security_desc, ::LocalFree);
  SECURITY_ATTRIBUTES security_attributes = {sizeof(SECURITY_ATTRIBUTES),
                                             security_desc, FALSE};

  const DWORD kOpenMode = enforce_uniqueness
                              ? PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                    FILE_FLAG_FIRST_PIPE_INSTANCE
                              : PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
  const DWORD kPipeMode =
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS;
  PlatformHandle handle(
      CreateNamedPipeW(named_handle.pipe_name().c_str(), kOpenMode, kPipeMode,
                       enforce_uniqueness ? 1 : 255,  // Max instances.
                       4096,                          // Out buffer size.
                       4096,                          // In buffer size.
                       5000,  // Timeout in milliseconds.
                       &security_attributes));
  handle.needs_connection = true;
  return ScopedPlatformHandle(handle);
}

}  // namespace edk
}  // namespace mojo
