// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/container_finder.h"

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/session/session_controller.h"
#include "ash/shell.h"
#include "ash/wm/always_on_top_controller.h"
#include "ash/wm/root_window_finder.h"
#include "ash/wm/window_state.h"
#include "ash/wm_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/geometry/rect.h"

namespace ash {
namespace wm {
namespace {

WmWindow* FindContainerRoot(const gfx::Rect& bounds) {
  if (bounds == gfx::Rect())
    return Shell::GetWmRootWindowForNewWindows();
  return GetRootWindowMatching(bounds);
}

bool HasTransientParentWindow(const WmWindow* window) {
  return window->GetTransientParent() &&
         window->GetTransientParent()->GetType() != ui::wm::WINDOW_TYPE_UNKNOWN;
}

WmWindow* GetSystemModalContainer(WmWindow* root, WmWindow* window) {
  DCHECK(window->IsSystemModal());

  // If screen lock is not active and user session is active,
  // all modal windows are placed into the normal modal container.
  // In case of missing transient parent (it could happen for alerts from
  // background pages) assume that the window belongs to user session.
  if (!Shell::Get()->session_controller()->IsUserSessionBlocked() ||
      !window->GetTransientParent()) {
    return root->GetChildByShellWindowId(kShellWindowId_SystemModalContainer);
  }

  // Otherwise those that originate from LockScreen container and above are
  // placed in the screen lock modal container.
  int window_container_id =
      window->GetTransientParent()->GetParent()->aura_window()->id();
  if (window_container_id < kShellWindowId_LockScreenContainer)
    return root->GetChildByShellWindowId(kShellWindowId_SystemModalContainer);
  return root->GetChildByShellWindowId(kShellWindowId_LockSystemModalContainer);
}

WmWindow* GetContainerFromAlwaysOnTopController(WmWindow* root,
                                                WmWindow* window) {
  return root->GetRootWindowController()
      ->always_on_top_controller()
      ->GetContainer(window);
}

}  // namespace

WmWindow* GetContainerForWindow(WmWindow* window) {
  WmWindow* parent = window->GetParent();
  // The first parent with an explicit shell window ID is the container.
  while (parent && parent->aura_window()->id() == kShellWindowId_Invalid)
    parent = parent->GetParent();
  return parent;
}

WmWindow* GetDefaultParent(WmWindow* window, const gfx::Rect& bounds) {
  WmWindow* target_root = nullptr;
  WmWindow* transient_parent = window->GetTransientParent();
  if (transient_parent) {
    // Transient window should use the same root as its transient parent.
    target_root = transient_parent->GetRootWindow();
  } else {
    target_root = FindContainerRoot(bounds);
  }

  switch (window->GetType()) {
    case ui::wm::WINDOW_TYPE_NORMAL:
    case ui::wm::WINDOW_TYPE_POPUP:
      if (window->IsSystemModal())
        return GetSystemModalContainer(target_root, window);
      if (HasTransientParentWindow(window))
        return GetContainerForWindow(window->GetTransientParent());
      return GetContainerFromAlwaysOnTopController(target_root, window);
    case ui::wm::WINDOW_TYPE_CONTROL:
      return target_root->GetChildByShellWindowId(
          kShellWindowId_UnparentedControlContainer);
    case ui::wm::WINDOW_TYPE_PANEL:
      if (window->aura_window()->GetProperty(kPanelAttachedKey))
        return target_root->GetChildByShellWindowId(
            kShellWindowId_PanelContainer);
      return GetContainerFromAlwaysOnTopController(target_root, window);
    case ui::wm::WINDOW_TYPE_MENU:
      return target_root->GetChildByShellWindowId(kShellWindowId_MenuContainer);
    case ui::wm::WINDOW_TYPE_TOOLTIP:
      return target_root->GetChildByShellWindowId(
          kShellWindowId_DragImageAndTooltipContainer);
    default:
      NOTREACHED() << "Window " << window->aura_window()->id()
                   << " has unhandled type " << window->GetType();
      break;
  }
  return nullptr;
}

aura::Window::Windows GetContainersFromAllRootWindows(
    int container_id,
    aura::Window* priority_root) {
  aura::Window::Windows containers;
  for (aura::Window* root : Shell::GetAllRootWindows()) {
    aura::Window* container = root->GetChildById(container_id);
    if (!container)
      continue;

    if (priority_root && priority_root->Contains(container))
      containers.insert(containers.begin(), container);
    else
      containers.push_back(container);
  }
  return containers;
}

}  // namespace wm
}  // namespace ash
