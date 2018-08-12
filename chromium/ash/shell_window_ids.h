// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_WINDOW_IDS_H_
#define ASH_SHELL_WINDOW_IDS_H_

#include "ash/wm/common/wm_shell_window_ids.h"

// Declarations of ids of special shell windows.

namespace ash {

// A higher-level container that holds all of the containers stacked below
// kShellWindowId_LockScreenContainer.  Only used by PowerButtonController for
// animating lower-level containers.
const int kShellWindowId_NonLockScreenContainersContainer = 0;

// A higher-level container that holds containers that hold lock-screen
// windows.  Only used by PowerButtonController for animating lower-level
// containers.
const int kShellWindowId_LockScreenContainersContainer = 1;

// A higher-level container that holds containers that hold lock-screen-related
// windows (which we want to display while the screen is locked; effectively
// containers stacked above kShellWindowId_LockSystemModalContainer).  Only used
// by PowerButtonController for animating lower-level containers.
const int kShellWindowId_LockScreenRelatedContainersContainer = 2;

// A container used for windows of WINDOW_TYPE_CONTROL that have no parent.
// This container is not visible. Defined in wm_shell_window_ids.
// kShellWindowId_UnparentedControlContainer = 3;

// The desktop background window.
const int kShellWindowId_DesktopBackgroundContainer = 4;

// The virtual keyboard container.
const int kShellWindowId_VirtualKeyboardContainer = 5;

// The container for standard top-level windows. Defined in wm_shell_window_ids.
// kShellWindowId_DefaultContainer = 6;

// The container for top-level windows with the 'always-on-top' flag set.
// kShellWindowId_AlwaysOnTopContainer = 7;

// The container for windows docked to either side of the desktop. Defined in
// wm_shell_window_ids.
// kShellWindowId_DockedContainer = 8;

// The container for the shelf. Defined in wm_shell_window_ids.
// kShellWindowId_ShelfContainer = 9;

// The container for bubbles which float over the shelf.
const int kShellWindowId_ShelfBubbleContainer = 10;

// The container for panel windows. Defined in wm_shell_window_ids.
// kShellWindowId_PanelContainer = 11;

// The container for the app list. Defined in wm_shell_window_ids.
// kShellWindowId_AppListContainer = 12;

// The container for user-specific modal windows. Defined in
// wm_shell_window_ids
// kShellWindowId_SystemModalContainer = 13;

// The container for the lock screen background.
const int kShellWindowId_LockScreenBackgroundContainer = 14;

// The container for the lock screen. Defined in wm_shell_window_ids.
// kShellWindowId_LockScreenContainer = 15;

// The container for the lock screen modal windows. Defined in
// wm_shell_window_ids.
// kShellWindowId_LockSystemModalContainer = 16;

// The container for the status area.
const int kShellWindowId_StatusContainer = 17;

// A parent container that holds the virtual keyboard container and ime windows
// if any. This is to ensure that the virtual keyboard or ime window is stacked
// above most containers but below the mouse cursor and the power off animation.
const int kShellWindowId_ImeWindowParentContainer = 18;

// The container for menus. Defined in wm_shell_window_ids.
// kShellWindowId_MenuContainer = 19;

// The container for drag/drop images and tooltips. Defined in
// wm_shell_window_ids.
// const int kShellWindowId_DragImageAndTooltipContainer = 20;

// The container for bubbles briefly overlaid onscreen to show settings changes
// (volume, brightness, input method bubbles, etc.).
const int kShellWindowId_SettingBubbleContainer = 21;

// The container for special components overlaid onscreen, such as the
// region selector for partial screenshots.
const int kShellWindowId_OverlayContainer = 22;

// ID of the window created by PhantomWindowController or DragWindowController.
// Defined in wm_shell_window_ids.
// kShellWindowId_PhantomWindow = 23;

// The container for mouse cursor.
const int kShellWindowId_MouseCursorContainer = 24;

// The topmost container, used for power off animation.
const int kShellWindowId_PowerButtonAnimationContainer = 25;

static_assert((kShellWindowId_UnparentedControlContainer - 1 ==
               kShellWindowId_LockScreenRelatedContainersContainer) &&
                  (kShellWindowId_UnparentedControlContainer + 1 ==
                   kShellWindowId_DesktopBackgroundContainer),
              "unparented-control between lock-screen-related and "
              "desktop-background");

static_assert((kShellWindowId_DefaultContainer - 1 ==
               kShellWindowId_VirtualKeyboardContainer) &&
                  (kShellWindowId_DefaultContainer + 1 ==
                   kShellWindowId_AlwaysOnTopContainer),
              "default between keyboard and always-on-top");

static_assert((kShellWindowId_AlwaysOnTopContainer - 1 ==
               kShellWindowId_DefaultContainer) &&
                  (kShellWindowId_AlwaysOnTopContainer + 1 ==
                   kShellWindowId_DockedContainer),
              "always-on-top between default and docked");

static_assert((kShellWindowId_DockedContainer - 1 ==
               kShellWindowId_AlwaysOnTopContainer) &&
                  (kShellWindowId_DockedContainer + 1 ==
                   kShellWindowId_ShelfContainer),
              "docked between always-on-top and shelf");

static_assert((kShellWindowId_ShelfContainer - 1 ==
               kShellWindowId_DockedContainer) &&
                  (kShellWindowId_ShelfContainer + 1 ==
                   kShellWindowId_ShelfBubbleContainer),
              "shelf between docked and shelf-bubble");

static_assert((kShellWindowId_PanelContainer - 1 ==
               kShellWindowId_ShelfBubbleContainer) &&
                  (kShellWindowId_PanelContainer + 1 ==
                   kShellWindowId_AppListContainer),
              "panel between shelf-bubble and app-list");

static_assert((kShellWindowId_AppListContainer - 1 ==
               kShellWindowId_PanelContainer) &&
                  (kShellWindowId_AppListContainer + 1 ==
                   kShellWindowId_SystemModalContainer),
              "app-list between panel and system-modal");

static_assert((kShellWindowId_SystemModalContainer - 1 ==
               kShellWindowId_AppListContainer) &&
                  (kShellWindowId_SystemModalContainer + 1 ==
                   kShellWindowId_LockScreenBackgroundContainer),
              "system-modal between app-list and lock-screen-background");

static_assert((kShellWindowId_LockScreenContainer - 1 ==
               kShellWindowId_LockScreenBackgroundContainer) &&
                  (kShellWindowId_LockScreenContainer + 1 ==
                   kShellWindowId_LockSystemModalContainer),
              "lock-screen between lock-screen-background and "
              "lock-screen-system-modal");

static_assert((kShellWindowId_LockSystemModalContainer - 1 ==
               kShellWindowId_LockScreenContainer) &&
                  (kShellWindowId_LockSystemModalContainer + 1 ==
                   kShellWindowId_StatusContainer),
              "lock-screen-system-modal between lock-screen and status");

static_assert((kShellWindowId_MenuContainer - 1 ==
               kShellWindowId_ImeWindowParentContainer) &&
                  (kShellWindowId_MenuContainer + 1 ==
                   kShellWindowId_DragImageAndTooltipContainer),
              "app-list between panel and system-modal");

static_assert((kShellWindowId_DragImageAndTooltipContainer - 1 ==
               kShellWindowId_MenuContainer) &&
                  (kShellWindowId_DragImageAndTooltipContainer + 1 ==
                   kShellWindowId_SettingBubbleContainer),
              "drag-image-and-tooltip between menu and settings-bubble");

static_assert((kShellWindowId_PhantomWindow - 1 ==
               kShellWindowId_OverlayContainer) &&
                  (kShellWindowId_PhantomWindow + 1 ==
                   kShellWindowId_MouseCursorContainer),
              "phanton between overlay and mouse-cursor");

}  // namespace ash

#endif  // ASH_SHELL_WINDOW_IDS_H_
