// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/gestures/shelf_gesture_handler.h"

#include "ash/common/session/session_state_delegate.h"
#include "ash/common/shelf/shelf_types.h"
#include "ash/common/wm/window_state.h"
#include "ash/common/wm_shell.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/system/status_area_widget.h"
#include "ash/wm/window_state_aura.h"
#include "ash/wm/window_util.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/transform.h"
#include "ui/views/widget/widget.h"

namespace ash {

ShelfGestureHandler::ShelfGestureHandler() : drag_in_progress_(false) {}

ShelfGestureHandler::~ShelfGestureHandler() {}

bool ShelfGestureHandler::ProcessGestureEvent(
    const ui::GestureEvent& event,
    const aura::Window* event_target_window) {
  // The gestures are disabled in the lock/login screen.
  SessionStateDelegate* delegate = WmShell::Get()->GetSessionStateDelegate();
  if (!delegate->NumberOfLoggedInUsers() || delegate->IsScreenLocked())
    return false;

  RootWindowController* controller =
      RootWindowController::ForWindow(event_target_window);
  ShelfLayoutManager* shelf = controller->GetShelfLayoutManager();

  if (event.type() == ui::ET_GESTURE_WIN8_EDGE_SWIPE) {
    shelf->OnGestureEdgeSwipe(event);
    return true;
  }

  const aura::Window* fullscreen = controller->GetWindowForFullscreenMode();
  if (fullscreen &&
      wm::GetWindowState(fullscreen)->shelf_mode_in_fullscreen() ==
          wm::WindowState::SHELF_HIDDEN) {
    return false;
  }

  if (event.type() == ui::ET_GESTURE_SCROLL_BEGIN) {
    drag_in_progress_ = true;
    shelf->StartGestureDrag(event);
    return true;
  }

  if (!drag_in_progress_)
    return false;

  if (event.type() == ui::ET_GESTURE_SCROLL_UPDATE) {
    shelf->UpdateGestureDrag(event);
    return true;
  }

  drag_in_progress_ = false;

  if (event.type() == ui::ET_GESTURE_SCROLL_END ||
      event.type() == ui::ET_SCROLL_FLING_START) {
    shelf->CompleteGestureDrag(event);
    return true;
  }

  // Unexpected event. Reset the state and let the event fall through.
  shelf->CancelGestureDrag();
  return false;
}

}  // namespace ash
