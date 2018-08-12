// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/tray/system_tray_controller.h"

#include "ash/common/system/tray/system_tray_notifier.h"
#include "ash/common/wm_shell.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "services/shell/public/cpp/connector.h"

namespace ash {

SystemTrayController::SystemTrayController(shell::Connector* connector)
    : connector_(connector), hour_clock_type_(base::GetHourClockType()) {}

SystemTrayController::~SystemTrayController() {}

void SystemTrayController::ShowSettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowSettings();
}

void SystemTrayController::ShowDateSettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowDateSettings();
}

void SystemTrayController::ShowDisplaySettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowDisplaySettings();
}

void SystemTrayController::ShowPowerSettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowPowerSettings();
}

void SystemTrayController::ShowChromeSlow() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowChromeSlow();
}

void SystemTrayController::ShowIMESettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowIMESettings();
}

void SystemTrayController::ShowHelp() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowHelp();
}

void SystemTrayController::ShowAccessibilityHelp() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowAccessibilityHelp();
}

void SystemTrayController::ShowAccessibilitySettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowAccessibilitySettings();
}

void SystemTrayController::ShowPaletteHelp() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowPaletteHelp();
}

void SystemTrayController::ShowPaletteSettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowPaletteSettings();
}

void SystemTrayController::ShowPublicAccountInfo() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowPublicAccountInfo();
}

void SystemTrayController::ShowNetworkSettings(const std::string& network_id) {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowNetworkSettings(network_id);
}

void SystemTrayController::ShowProxySettings() {
  if (ConnectToSystemTrayClient())
    system_tray_client_->ShowProxySettings();
}

void SystemTrayController::BindRequest(mojom::SystemTrayRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

bool SystemTrayController::ConnectToSystemTrayClient() {
  // Unit tests may not have a connector.
  if (!connector_)
    return false;

#if defined(OS_CHROMEOS)
  // If already connected, nothing to do.
  if (system_tray_client_.is_bound())
    return true;

  // Connect (or reconnect) to the interface.
  if (WmShell::Get()->IsRunningInMash()) {
    connector_->ConnectToInterface("exe:chrome", &system_tray_client_);
  } else {
    connector_->ConnectToInterface("service:content_browser",
                                   &system_tray_client_);
  }

  // Handle chrome crashes by forcing a reconnect on the next request.
  system_tray_client_.set_connection_error_handler(base::Bind(
      &SystemTrayController::OnClientConnectionError, base::Unretained(this)));
  return true;
#else
  // The SystemTrayClient interface in the browser is only implemented for
  // Chrome OS, so don't try to connect on other platforms.
  return false;
#endif  // defined(OS_CHROMEOS)
}

void SystemTrayController::OnClientConnectionError() {
  system_tray_client_.reset();
}

void SystemTrayController::SetUse24HourClock(bool use_24_hour) {
  hour_clock_type_ = use_24_hour ? base::k24HourClock : base::k12HourClock;
  WmShell::Get()->system_tray_notifier()->NotifyDateFormatChanged();
}

}  // namespace ash
