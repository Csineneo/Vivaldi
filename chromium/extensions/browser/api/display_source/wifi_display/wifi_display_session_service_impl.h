// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_DISPLAY_SOURCE_WIFI_DISPLAY_WIFI_DISPLAY_SESSION_SERVICE_IMPL_H_
#define EXTENSIONS_BROWSER_API_DISPLAY_SOURCE_WIFI_DISPLAY_WIFI_DISPLAY_SESSION_SERVICE_IMPL_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "extensions/browser/api/display_source/display_source_connection_delegate.h"
#include "extensions/common/mojo/wifi_display_session_service.mojom.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace extensions {

// This class provides access to the network transport for the Wi-Fi Display
// session (which is itself hosted in the sandboxed renderer process).
class WiFiDisplaySessionServiceImpl
    : public WiFiDisplaySessionService,
      public DisplaySourceConnectionDelegate::Observer {
 public:
  ~WiFiDisplaySessionServiceImpl() override;
  static void BindToRequest(
      content::BrowserContext* context,
      mojo::InterfaceRequest<WiFiDisplaySessionService> request);

 private:
  // WiFiDisplaySessionService overrides.
  void SetClient(WiFiDisplaySessionServiceClientPtr client) override;
  void Connect(int32_t sink_id,
               int32_t auth_method,
               const mojo::String& auth_data) override;
  void Disconnect() override;
  void SendMessage(const mojo::String& message) override;

  // DisplaySourceConnectionDelegate::Observer overrides.
  void OnSinksUpdated(const DisplaySourceSinkInfoList& sinks) override;

  explicit WiFiDisplaySessionServiceImpl(
      DisplaySourceConnectionDelegate* delegate,
      mojo::InterfaceRequest<WiFiDisplaySessionService> request);

  void OnConnectFailed(int sink_id, const std::string& message);
  void OnDisconnectFailed(int sink_id, const std::string& message);

  void OnClientConnectionError();

  mojo::StrongBinding<WiFiDisplaySessionService> binding_;
  WiFiDisplaySessionServiceClientPtr client_;
  DisplaySourceConnectionDelegate* delegate_;

  // Id of the currenty connected sink (if any), obtained from connection
  // delegate. Keep it so that we know if a session has been ended.
  int last_connected_sink_;
  // Id of the sink of the session this object is associated with.
  int own_sink_;

  base::WeakPtrFactory<WiFiDisplaySessionServiceImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WiFiDisplaySessionServiceImpl);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_DISPLAY_SOURCE_WIFI_DISPLAY_WIFI_DISPLAY_SESSION_SERVICE_IMPL_H_
