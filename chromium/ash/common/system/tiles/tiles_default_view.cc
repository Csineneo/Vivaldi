// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/tiles/tiles_default_view.h"

#include "ash/common/metrics/user_metrics_action.h"
#include "ash/common/session/session_state_delegate.h"
#include "ash/common/system/tray/system_menu_button.h"
#include "ash/common/system/tray/system_tray.h"
#include "ash/common/system/tray/system_tray_controller.h"
#include "ash/common/system/tray/system_tray_delegate.h"
#include "ash/common/system/tray/system_tray_item.h"
#include "ash/common/system/tray/tray_constants.h"
#include "ash/common/wm_shell.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "grit/ash_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/border.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"

#if defined(OS_CHROMEOS)
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#endif

namespace {

// The ISO-639 code for the Hebrew locale. The help icon asset is a '?' which is
// not mirrored in this locale.
const char kHebrewLocale[] = "he";

}  // namespace

namespace ash {

TilesDefaultView::TilesDefaultView(SystemTrayItem* owner)
    : owner_(owner),
      settings_button_(nullptr),
      help_button_(nullptr),
      lock_button_(nullptr),
      power_button_(nullptr),
      weak_factory_(this) {}

TilesDefaultView::~TilesDefaultView() {
  SystemTrayDelegate* system_tray_delegate =
      WmShell::Get()->system_tray_delegate();

  // Perform this check since the delegate is destroyed first upon shell
  // destruction.
  if (system_tray_delegate)
    system_tray_delegate->RemoveShutdownPolicyObserver(this);
}

void TilesDefaultView::Init() {
  WmShell* shell = WmShell::Get();
  views::BoxLayout* box_layout =
      new views::BoxLayout(views::BoxLayout::kHorizontal, 4, 0, 0);
  box_layout->set_main_axis_alignment(
      views::BoxLayout::MAIN_AXIS_ALIGNMENT_START);
  box_layout->set_cross_axis_alignment(
      views::BoxLayout::CROSS_AXIS_ALIGNMENT_CENTER);
  SetLayoutManager(box_layout);

  if (shell->system_tray_delegate()->ShouldShowSettings()) {
    settings_button_ = new SystemMenuButton(this, kSystemMenuSettingsIcon,
                                            IDS_ASH_STATUS_TRAY_SETTINGS);
    AddChildView(settings_button_);
    AddSeparator();
  }

  help_button_ =
      new SystemMenuButton(this, kSystemMenuHelpIcon, IDS_ASH_STATUS_TRAY_HELP);
  if (base::i18n::IsRTL() &&
      base::i18n::GetConfiguredLocale() == kHebrewLocale) {
    // The asset for the help button is a question mark '?'. Normally this asset
    // is flipped in RTL locales, however Hebrew uses the LTR '?'. So the
    // flipping must be disabled. (crbug.com/475237)
    help_button_->EnableCanvasFlippingForRTLUI(false);
  }
  AddChildView(help_button_);

#if !defined(OS_WIN)
  if (shell->GetSessionStateDelegate()->CanLockScreen()) {
    AddSeparator();
    lock_button_ = new SystemMenuButton(this, kSystemMenuLockIcon,
                                        IDS_ASH_STATUS_TRAY_LOCK);
    AddChildView(lock_button_);
  }

  AddSeparator();
  power_button_ = new SystemMenuButton(this, kSystemMenuPowerIcon,
                                       IDS_ASH_STATUS_TRAY_SHUTDOWN);
  AddChildView(power_button_);

  SystemTrayDelegate* system_tray_delegate = shell->system_tray_delegate();
  system_tray_delegate->AddShutdownPolicyObserver(this);
  system_tray_delegate->ShouldRebootOnShutdown(base::Bind(
      &TilesDefaultView::OnShutdownPolicyChanged, weak_factory_.GetWeakPtr()));
#endif  // !defined(OS_WIN)
}

void TilesDefaultView::ButtonPressed(views::Button* sender,
                                     const ui::Event& event) {
  DCHECK(sender);
  WmShell* shell = WmShell::Get();
  if (sender == settings_button_) {
    shell->RecordUserMetricsAction(UMA_TRAY_SETTINGS);
    shell->system_tray_controller()->ShowSettings();
  } else if (sender == help_button_) {
    shell->RecordUserMetricsAction(UMA_TRAY_HELP);
    shell->system_tray_controller()->ShowHelp();
  } else if (sender == lock_button_) {
    shell->RecordUserMetricsAction(UMA_TRAY_LOCK_SCREEN);
#if defined(OS_CHROMEOS)
    chromeos::DBusThreadManager::Get()
        ->GetSessionManagerClient()
        ->RequestLockScreen();
#endif
  } else if (sender == power_button_) {
    shell->RecordUserMetricsAction(UMA_TRAY_SHUT_DOWN);
    shell->system_tray_delegate()->RequestShutdown();
  }

  owner_->system_tray()->CloseSystemBubble();
}

void TilesDefaultView::OnShutdownPolicyChanged(bool reboot_on_shutdown) {
  if (!power_button_)
    return;

  power_button_->SetTooltipText(l10n_util::GetStringUTF16(
      reboot_on_shutdown ? IDS_ASH_STATUS_TRAY_REBOOT
                         : IDS_ASH_STATUS_TRAY_SHUTDOWN));
}

void TilesDefaultView::AddSeparator() {
  views::Separator* separator =
      new views::Separator(views::Separator::HORIZONTAL);
  separator->SetPreferredSize(kHorizontalSeparatorHeight);
  separator->SetColor(kHorizontalSeparatorColor);
  AddChildView(separator);
}

}  // namespace ash
