// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_IPC_CONSTANTS_H_
#define REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_IPC_CONSTANTS_H_

#include <string>

namespace remoting {

// Used to indicate an error during remote security key forwarding session.
extern const char kRemoteSecurityKeyConnectionError[];

// Returns the name of the well-known IPC server channel used to initiate a
// remote security key forwarding session.
const std::string& GetRemoteSecurityKeyIpcChannelName();

// Sets the name of the well-known IPC server channel for testing purposes.
void SetRemoteSecurityKeyIpcChannelNameForTest(const std::string& channel_name);

}  // namespace remoting

#endif  // REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_IPC_CONSTANTS_H_
