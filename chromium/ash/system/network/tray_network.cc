// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/network/tray_network.h"

#include "ash/shelf/wm_shelf_util.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/network/network_icon.h"
#include "ash/system/network/network_icon_animation.h"
#include "ash/system/network/network_icon_animation_observer.h"
#include "ash/system/network/network_state_list_detailed_view.h"
#include "ash/system/network/tray_network_state_observer.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_item_more.h"
#include "ash/system/tray/tray_item_view.h"
#include "ash/system/tray/tray_popup_item_style.h"
#include "ash/system/tray/tray_utils.h"
#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/link_listener.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

using chromeos::NetworkHandler;
using chromeos::NetworkState;
using chromeos::NetworkStateHandler;
using chromeos::NetworkTypePattern;

namespace ash {
namespace tray {

namespace {

// Returns the connected, non-virtual (aka VPN), network.
const NetworkState* GetConnectedNetwork() {
  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();
  return handler->ConnectedNetworkByType(NetworkTypePattern::NonVirtual());
}

}  // namespace

class NetworkTrayView : public TrayItemView,
                        public network_icon::AnimationObserver {
 public:
  explicit NetworkTrayView(TrayNetwork* network_tray)
      : TrayItemView(network_tray) {
    CreateImageView();
    UpdateNetworkStateHandlerIcon();
  }

  ~NetworkTrayView() override {
    network_icon::NetworkIconAnimation::GetInstance()->RemoveObserver(this);
  }

  const char* GetClassName() const override { return "NetworkTrayView"; }

  void UpdateNetworkStateHandlerIcon() {
    gfx::ImageSkia image;
    base::string16 name;
    bool animating = false;
    network_icon::GetDefaultNetworkImageAndLabel(network_icon::ICON_TYPE_TRAY,
                                                 &image, &name, &animating);
    bool show_in_tray = !image.isNull();
    UpdateIcon(show_in_tray, image);
    if (animating)
      network_icon::NetworkIconAnimation::GetInstance()->AddObserver(this);
    else
      network_icon::NetworkIconAnimation::GetInstance()->RemoveObserver(this);
    // Update accessibility.
    const NetworkState* connected_network = GetConnectedNetwork();
    if (connected_network) {
      UpdateConnectionStatus(base::UTF8ToUTF16(connected_network->name()),
                             true);
    } else {
      UpdateConnectionStatus(base::string16(), false);
    }
  }

  // views::View:
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override {
    node_data->SetName(connection_status_string_);
    node_data->role = ui::AX_ROLE_BUTTON;
  }

  // network_icon::AnimationObserver:
  void NetworkIconChanged() override { UpdateNetworkStateHandlerIcon(); }

 private:
  // Updates connection status and notifies accessibility event when necessary.
  void UpdateConnectionStatus(const base::string16& network_name,
                              bool connected) {
    base::string16 new_connection_status_string;
    if (connected) {
      new_connection_status_string = l10n_util::GetStringFUTF16(
          IDS_ASH_STATUS_TRAY_NETWORK_CONNECTED, network_name);
    }
    if (new_connection_status_string != connection_status_string_) {
      connection_status_string_ = new_connection_status_string;
      if (!connection_status_string_.empty())
        NotifyAccessibilityEvent(ui::AX_EVENT_ALERT, true);
    }
  }

  void UpdateIcon(bool tray_icon_visible, const gfx::ImageSkia& image) {
    image_view()->SetImage(image);
    SetVisible(tray_icon_visible);
    SchedulePaint();
  }

  base::string16 connection_status_string_;

  DISALLOW_COPY_AND_ASSIGN(NetworkTrayView);
};

class NetworkDefaultView : public TrayItemMore,
                           public network_icon::AnimationObserver {
 public:
  explicit NetworkDefaultView(TrayNetwork* network_tray)
      : TrayItemMore(network_tray) {
    Update();
  }

  ~NetworkDefaultView() override {
    network_icon::NetworkIconAnimation::GetInstance()->RemoveObserver(this);
  }

  void Update() {
    gfx::ImageSkia image;
    base::string16 label;
    bool animating = false;
    network_icon::GetDefaultNetworkImageAndLabel(
        network_icon::ICON_TYPE_DEFAULT_VIEW, &image, &label, &animating);
    // We use the inactive icon alpha only if there is no active network and
    // wifi is disabled.
    if (!IsActive() &&
        !NetworkHandler::Get()->network_state_handler()->IsTechnologyEnabled(
            NetworkTypePattern::WiFi())) {
      image = gfx::ImageSkiaOperations::CreateTransparentImage(
          image, TrayPopupItemStyle::kInactiveIconAlpha);
    }

    if (animating)
      network_icon::NetworkIconAnimation::GetInstance()->AddObserver(this);
    else
      network_icon::NetworkIconAnimation::GetInstance()->RemoveObserver(this);
    SetImage(image);
    SetLabel(label);
    SetAccessibleName(label);
    UpdateStyle();
  }

  // network_icon::AnimationObserver
  void NetworkIconChanged() override { Update(); }

 protected:
  // TrayItemMore:
  std::unique_ptr<TrayPopupItemStyle> HandleCreateStyle() const override {
    std::unique_ptr<TrayPopupItemStyle> style =
        TrayItemMore::HandleCreateStyle();
    style->set_color_style(IsActive()
                               ? TrayPopupItemStyle::ColorStyle::ACTIVE
                               : TrayPopupItemStyle::ColorStyle::INACTIVE);
    return style;
  }

  // Determines whether to use the ACTIVE or INACTIVE text style.
  bool IsActive() const { return GetConnectedNetwork() != nullptr; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkDefaultView);
};

class NetworkWifiDetailedView : public NetworkDetailedView {
 public:
  explicit NetworkWifiDetailedView(SystemTrayItem* owner)
      : NetworkDetailedView(owner) {}

  ~NetworkWifiDetailedView() override {}

  // NetworkDetailedView:
  void Init() override {
    constexpr int kVerticalPadding = 10;
    auto* box_layout = new views::BoxLayout(
        views::BoxLayout::kHorizontal, kTrayPopupPaddingHorizontal,
        kVerticalPadding, kTrayPopupPaddingBetweenItems);
    SetLayoutManager(box_layout);

    image_view_ = new views::ImageView;
    AddChildView(image_view_);

    label_view_ = new views::Label();
    label_view_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    AddChildView(label_view_);
    box_layout->SetFlexForView(label_view_, 1);

    Update();
  }

  NetworkDetailedView::DetailedViewType GetViewType() const override {
    return NetworkDetailedView::WIFI_VIEW;
  }

  void Update() override {
    const bool wifi_enabled =
        NetworkHandler::Get()->network_state_handler()->IsTechnologyEnabled(
            NetworkTypePattern::WiFi());
    image_view_->SetImage(
        network_icon::GetImageForWiFiEnabledState(wifi_enabled));

    const int string_id = wifi_enabled
                              ? IDS_ASH_STATUS_TRAY_NETWORK_WIFI_ENABLED
                              : IDS_ASH_STATUS_TRAY_NETWORK_WIFI_DISABLED;
    label_view_->SetText(l10n_util::GetStringUTF16(string_id));
  }

 private:
  views::ImageView* image_view_ = nullptr;
  views::Label* label_view_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(NetworkWifiDetailedView);
};

}  // namespace tray

TrayNetwork::TrayNetwork(SystemTray* system_tray)
    : SystemTrayItem(system_tray, UMA_NETWORK),
      tray_(NULL),
      default_(NULL),
      detailed_(NULL),
      request_wifi_view_(false) {
  network_state_observer_.reset(new TrayNetworkStateObserver(this));
  SystemTrayNotifier* notifier = Shell::Get()->system_tray_notifier();
  notifier->AddNetworkObserver(this);
  notifier->AddNetworkPortalDetectorObserver(this);
}

TrayNetwork::~TrayNetwork() {
  SystemTrayNotifier* notifier = Shell::Get()->system_tray_notifier();
  notifier->RemoveNetworkObserver(this);
  notifier->RemoveNetworkPortalDetectorObserver(this);
}

views::View* TrayNetwork::CreateTrayView(LoginStatus status) {
  CHECK(tray_ == NULL);
  if (!chromeos::NetworkHandler::IsInitialized())
    return NULL;
  tray_ = new tray::NetworkTrayView(this);
  return tray_;
}

views::View* TrayNetwork::CreateDefaultView(LoginStatus status) {
  CHECK(default_ == NULL);
  if (!chromeos::NetworkHandler::IsInitialized())
    return NULL;
  CHECK(tray_ != NULL);
  default_ = new tray::NetworkDefaultView(this);
  default_->SetEnabled(status != LoginStatus::LOCKED);
  return default_;
}

views::View* TrayNetwork::CreateDetailedView(LoginStatus status) {
  CHECK(detailed_ == NULL);
  ShellPort::Get()->RecordUserMetricsAction(
      UMA_STATUS_AREA_DETAILED_NETWORK_VIEW);
  if (!chromeos::NetworkHandler::IsInitialized())
    return NULL;
  if (request_wifi_view_) {
    detailed_ = new tray::NetworkWifiDetailedView(this);
    request_wifi_view_ = false;
  } else {
    detailed_ = new tray::NetworkStateListDetailedView(
        this, tray::NetworkStateListDetailedView::LIST_TYPE_NETWORK, status);
  }
  detailed_->Init();
  return detailed_;
}

void TrayNetwork::DestroyTrayView() {
  tray_ = NULL;
}

void TrayNetwork::DestroyDefaultView() {
  default_ = NULL;
}

void TrayNetwork::DestroyDetailedView() {
  detailed_ = NULL;
}

void TrayNetwork::RequestToggleWifi() {
  // This will always be triggered by a user action (e.g. keyboard shortcut)
  if (!detailed_ ||
      detailed_->GetViewType() == tray::NetworkDetailedView::WIFI_VIEW) {
    request_wifi_view_ = true;
    ShowDetailedView(kTrayPopupAutoCloseDelayForTextInSeconds, false);
  }
  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();
  bool enabled = handler->IsTechnologyEnabled(NetworkTypePattern::WiFi());
  ShellPort::Get()->RecordUserMetricsAction(
      enabled ? UMA_STATUS_AREA_DISABLE_WIFI : UMA_STATUS_AREA_ENABLE_WIFI);
  handler->SetTechnologyEnabled(NetworkTypePattern::WiFi(), !enabled,
                                chromeos::network_handler::ErrorCallback());
}

void TrayNetwork::OnCaptivePortalDetected(const std::string& /* guid */) {
  NetworkStateChanged();
}

void TrayNetwork::NetworkStateChanged() {
  if (tray_)
    tray_->UpdateNetworkStateHandlerIcon();
  if (default_)
    default_->Update();
  if (detailed_)
    detailed_->Update();
}

}  // namespace ash
