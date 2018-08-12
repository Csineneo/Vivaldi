// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/api/display_source/display_source_session.h"

#if defined(ENABLE_WIFI_DISPLAY)
#include "extensions/renderer/api/display_source/wifi_display/wifi_display_session.h"
#endif

namespace extensions {

DisplaySourceSessionParams::DisplaySourceSessionParams()
    : auth_method(api::display_source::AUTHENTICATION_METHOD_NONE) {
}

DisplaySourceSessionParams::~DisplaySourceSessionParams() = default;

DisplaySourceSession::DisplaySourceSession()
    : state_(Idle) {
}

DisplaySourceSession::~DisplaySourceSession() = default;

void DisplaySourceSession::SetCallbacks(
    const SinkIdCallback& started_callback,
    const SinkIdCallback& terminated_callback,
    const ErrorCallback& error_callback) {
  DCHECK(started_callback_.is_null());
  DCHECK(terminated_callback_.is_null());
  DCHECK(error_callback_.is_null());

  started_callback_ = started_callback;
  terminated_callback_ = terminated_callback;
  error_callback_ = error_callback;
}

scoped_ptr<DisplaySourceSession> DisplaySourceSessionFactory::CreateSession(
    const DisplaySourceSessionParams& params) {
#if defined(ENABLE_WIFI_DISPLAY)
  return scoped_ptr<DisplaySourceSession>(new WiFiDisplaySession(params));
#endif
  return nullptr;
}

}  // namespace extensions
