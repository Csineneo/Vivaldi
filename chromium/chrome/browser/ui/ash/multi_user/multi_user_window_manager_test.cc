// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_test.h"

#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/browser_window.h"
#include "ui/aura/window.h"

TestMultiUserWindowManager::TestMultiUserWindowManager(
    Browser* visiting_browser,
    const std::string& desktop_owner)
    : browser_window_(visiting_browser->window()->GetNativeWindow()),
      browser_owner_(
          multi_user_util::GetUserIDFromProfile(visiting_browser->profile())),
      desktop_owner_(desktop_owner),
      created_window_(NULL),
      created_window_shown_for_(browser_owner_),
      current_user_id_(desktop_owner) {
  // Register this object with the system (which will take ownership). It will
  // be deleted by ChromeLauncherController::~ChromeLauncherController().
  chrome::MultiUserWindowManager::SetInstanceForTest(
      this, chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED);
}

TestMultiUserWindowManager::~TestMultiUserWindowManager() {
  // This object is owned by the MultiUserWindowManager since the
  // SetInstanceForTest call. As such no uninstall is required.
}

void TestMultiUserWindowManager::SetWindowOwner(aura::Window* window,
                                                const std::string& user_id) {
  NOTREACHED();
}

const std::string& TestMultiUserWindowManager::GetWindowOwner(
    aura::Window* window) const {
  // No matter which window will get queried - all browsers belong to the
  // original browser's user.
  return browser_owner_;
}

void TestMultiUserWindowManager::ShowWindowForUser(aura::Window* window,
                                                   const std::string& user_id) {
  // This class is only able to handle one additional window <-> user
  // association beside the creation parameters.
  // If no association has yet been requested remember it now.
  DCHECK(!created_window_);
  created_window_ = window;
  created_window_shown_for_ = user_id;

  if (browser_window_ == window)
    desktop_owner_ = user_id;

  if (user_id == current_user_id_)
    return;

  // Change the visibility of the window to update the view recursively.
  window->Hide();
  window->Show();
  current_user_id_ = user_id;
}

bool TestMultiUserWindowManager::AreWindowsSharedAmongUsers() const {
  return browser_owner_ != desktop_owner_;
}

void TestMultiUserWindowManager::GetOwnersOfVisibleWindows(
    std::set<std::string>* user_ids) const {
}

bool TestMultiUserWindowManager::IsWindowOnDesktopOfUser(
    aura::Window* window,
    const std::string& user_id) const {
  return GetUserPresentingWindow(window) == user_id;
}

const std::string& TestMultiUserWindowManager::GetUserPresentingWindow(
    aura::Window* window) const {
  if (window == browser_window_)
    return desktop_owner_;
  if (created_window_ && window == created_window_)
    return created_window_shown_for_;
  // We can come here before the window gets registered.
  return browser_owner_;
}

void TestMultiUserWindowManager::AddUser(content::BrowserContext* profile) {
}

void TestMultiUserWindowManager::AddObserver(Observer* observer) {
}

void TestMultiUserWindowManager::RemoveObserver(Observer* observer) {
}
