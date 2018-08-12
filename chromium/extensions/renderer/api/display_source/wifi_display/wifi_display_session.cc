// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/api/display_source/wifi_display/wifi_display_session.h"

#include "base/logging.h"
#include "base/timer/timer.h"
#include "content/public/common/service_registry.h"
#include "content/public/renderer/render_frame.h"

namespace {
const char kErrorInternal[] = "An internal error has occurred";
}  // namespace

namespace extensions {

using api::display_source::ErrorType;

WiFiDisplaySession::WiFiDisplaySession(
    const DisplaySourceSessionParams& params)
  : binding_(this),
    params_(params),
    weak_factory_(this) {
  DCHECK(params_.render_frame);
  params.render_frame->GetServiceRegistry()->ConnectToRemoteService(
      mojo::GetProxy(&service_));
  service_.set_connection_error_handler(base::Bind(
          &WiFiDisplaySession::OnConnectionError,
          weak_factory_.GetWeakPtr()));

  WiFiDisplaySessionServiceClientPtr client_ptr;
  binding_.Bind(GetProxy(&client_ptr));
  binding_.set_connection_error_handler(base::Bind(
          &WiFiDisplaySession::OnConnectionError,
          weak_factory_.GetWeakPtr()));
  DCHECK(client_ptr);
  service_->SetClient(std::move(client_ptr));
}

WiFiDisplaySession::~WiFiDisplaySession() {
}

void WiFiDisplaySession::Start() {
  DCHECK(state_ == DisplaySourceSession::Idle);
  service_->Connect(params_.sink_id, params_.auth_method, params_.auth_data);
  state_ = DisplaySourceSession::Establishing;
}

void WiFiDisplaySession::Terminate() {
  DCHECK(state_ != DisplaySourceSession::Idle);
  switch (state_) {
  case DisplaySourceSession::Idle:
  case DisplaySourceSession::Terminating:
    // Nothing to do.
    return;
  case DisplaySourceSession::Establishing:
  case DisplaySourceSession::Established:
    service_->Disconnect();
    state_ = DisplaySourceSession::Terminating;
    break;
  default:
    NOTREACHED();
  }
}

void WiFiDisplaySession::OnConnected(
    int32_t sink_id, const mojo::String& ip_address) {
  if (sink_id == params_.sink_id) {
    DCHECK(state_ != DisplaySourceSession::Established);
    ip_address_ = ip_address;
    state_ = DisplaySourceSession::Established;
  }

  if (!started_callback_.is_null())
    started_callback_.Run(sink_id);
}

void WiFiDisplaySession::OnDisconnected(int32_t sink_id) {
  if (sink_id == params_.sink_id) {
    DCHECK(state_ == DisplaySourceSession::Established ||
           state_ == DisplaySourceSession::Terminating);
    state_ = DisplaySourceSession::Idle;
  }

  if (!terminated_callback_.is_null())
    terminated_callback_.Run(sink_id);
}

void WiFiDisplaySession::OnError(
    int32_t sink_id, int32_t type, const mojo::String& description) {
  DCHECK(type > api::display_source::ERROR_TYPE_NONE
         && type <= api::display_source::ERROR_TYPE_LAST);
  if (!error_callback_.is_null())
    error_callback_.Run(sink_id, static_cast<ErrorType>(type), description);
}

void WiFiDisplaySession::OnMessage(const mojo::String& data) {
  DCHECK(state_ == DisplaySourceSession::Established);
}

void WiFiDisplaySession::OnConnectionError() {
  if (!error_callback_.is_null()) {
    error_callback_.Run(params_.sink_id,
                        api::display_source::ERROR_TYPE_UNKNOWN_ERROR,
                        kErrorInternal);
  }

  if (state_ == DisplaySourceSession::Established ||
      state_ == DisplaySourceSession::Terminating) {
    // We must explicitly notify the session termination as it will never
    // arrive from browser process (IPC is broken).
    if (!terminated_callback_.is_null())
      terminated_callback_.Run(params_.sink_id);
  }
}

}  // namespace extensions
