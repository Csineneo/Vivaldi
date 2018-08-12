// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TRAY_DEFAULT_SYSTEM_TRAY_DELEGATE_H_
#define ASH_SYSTEM_TRAY_DEFAULT_SYSTEM_TRAY_DELEGATE_H_

#include "ash/ash_export.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "base/macros.h"

namespace ash {

class ASH_EXPORT DefaultSystemTrayDelegate : public SystemTrayDelegate {
 public:
  DefaultSystemTrayDelegate();
  ~DefaultSystemTrayDelegate() override;

  // SystemTrayDelegate:
  LoginStatus GetUserLoginStatus() const override;
  std::string GetSupervisedUserManager() const override;
  bool IsUserSupervised() const override;
  bool ShouldShowSettings() const override;
  bool ShouldShowNotificationTray() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(DefaultSystemTrayDelegate);
};

}  // namespace ash

#endif  // ASH_SYSTEM_TRAY_DEFAULT_SYSTEM_TRAY_DELEGATE_H_
