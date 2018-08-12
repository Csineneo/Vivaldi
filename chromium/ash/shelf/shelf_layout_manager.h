// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELF_SHELF_LAYOUT_MANAGER_H_
#define ASH_SHELF_SHELF_LAYOUT_MANAGER_H_

#include <memory>
#include <vector>

#include "ash/ash_export.h"
#include "ash/common/session/session_state_observer.h"
#include "ash/common/shelf/shelf_types.h"
#include "ash/common/shell_observer.h"
#include "ash/common/wm/background_animator.h"
#include "ash/common/wm/dock/docked_window_layout_manager_observer.h"
#include "ash/common/wm/window_state.h"
#include "ash/common/wm/workspace/workspace_types.h"
#include "ash/shelf/shelf.h"
#include "ash/snap_to_pixel_layout_manager.h"
#include "ash/system/status_area_widget.h"
#include "ash/wm/gestures/shelf_gesture_handler.h"
#include "ash/wm/lock_state_observer.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/timer/timer.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_controller_observer.h"
#include "ui/wm/public/activation_change_observer.h"

namespace aura {
class RootWindow;
}

namespace ui {
class GestureEvent;
class ImplicitAnimationObserver;
}

namespace ash {
class PanelLayoutManagerTest;
class ScreenAsh;
class ShelfBezelEventFilter;
class ShelfLayoutManagerObserver;
class ShelfLayoutManagerTest;
class ShelfWidget;
class StatusAreaWidget;
class WorkspaceController;
FORWARD_DECLARE_TEST(AshPopupAlignmentDelegateTest, AutoHide);
FORWARD_DECLARE_TEST(WebNotificationTrayTest, PopupAndFullscreen);

// ShelfLayoutManager is the layout manager responsible for the shelf and
// status widgets. The shelf is given the total available width and told the
// width of the status area. This allows the shelf to draw the background and
// layout to the status area.
// To respond to bounds changes in the status area StatusAreaLayoutManager works
// closely with ShelfLayoutManager.
// On mus, widget bounds management is handled by the window manager.
class ASH_EXPORT ShelfLayoutManager
    : public ash::ShellObserver,
      public aura::client::ActivationChangeObserver,
      public DockedWindowLayoutManagerObserver,
      public keyboard::KeyboardControllerObserver,
      public LockStateObserver,
      public SnapToPixelLayoutManager,
      public SessionStateObserver {
 public:
  explicit ShelfLayoutManager(ShelfWidget* shelf_widget);
  ~ShelfLayoutManager() override;

  void set_workspace_controller(WorkspaceController* controller) {
    workspace_controller_ = controller;
  }

  bool updating_bounds() const { return updating_bounds_; }

  // Clears internal data for shutdown process.
  void PrepareForShutdown();

  // Returns whether the shelf and its contents (shelf, status) are visible
  // on the screen.
  bool IsVisible() const;

  // Returns the ideal bounds of the shelf assuming it is visible.
  gfx::Rect GetIdealBounds();

  // Returns the preferred size of the shelf for the target visibility state.
  gfx::Size GetPreferredSize();

  // Returns the docked area bounds.
  const gfx::Rect& dock_bounds() const { return dock_bounds_; }

  // Returns the bounds within the root window not occupied by the shelf nor the
  // virtual keyboard.
  const gfx::Rect& user_work_area_bounds() const {
    return user_work_area_bounds_;
  }

  // Stops any animations and sets the bounds of the shelf and status widgets.
  void LayoutShelf();

  // Returns shelf visibility state based on current value of auto hide
  // behavior setting.
  ShelfVisibilityState CalculateShelfVisibility();

  // Updates the visibility state.
  void UpdateVisibilityState();

  // Invoked by the shelf when the auto-hide state may have changed.
  void UpdateAutoHideState();

  // Updates the auto-hide state for certain events. In classic ash these come
  // from an EventHandler. In mash these come from events that hit the shelf
  // widget and status tray widget.
  void UpdateAutoHideForMouseEvent(ui::MouseEvent* event);
  void UpdateAutoHideForGestureEvent(ui::GestureEvent* event);

  ShelfVisibilityState visibility_state() const {
    return state_.visibility_state;
  }
  ShelfAutoHideState auto_hide_state() const { return state_.auto_hide_state; }

  ShelfWidget* shelf_widget() { return shelf_widget_; }

  // Sets whether any windows overlap the shelf. If a window overlaps the shelf
  // the shelf renders slightly differently.
  void SetWindowOverlapsShelf(bool value);
  bool window_overlaps_shelf() const { return window_overlaps_shelf_; }

  void AddObserver(ShelfLayoutManagerObserver* observer);
  void RemoveObserver(ShelfLayoutManagerObserver* observer);

  // Gesture related functions:
  void OnGestureEdgeSwipe(const ui::GestureEvent& gesture);
  void StartGestureDrag(const ui::GestureEvent& gesture);
  void UpdateGestureDrag(const ui::GestureEvent& gesture);
  void CompleteGestureDrag(const ui::GestureEvent& gesture);
  void CancelGestureDrag();

  // Set an animation duration override for the show / hide animation of the
  // shelf. Specifying 0 leads to use the default.
  void SetAnimationDurationOverride(int duration_override_in_ms);

  // Overridden from SnapLayoutManager:
  void OnWindowResized() override;
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override;

  // Overridden from ash::ShellObserver:
  void OnLockStateChanged(bool locked) override;
  void OnShelfAlignmentChanged(WmWindow* root_window) override;
  void OnShelfAutoHideBehaviorChanged(WmWindow* root_window) override;
  void OnPinnedStateChanged(WmWindow* pinned_window) override;

  // Overriden from aura::client::ActivationChangeObserver:
  void OnWindowActivated(
      aura::client::ActivationChangeObserver::ActivationReason reason,
      aura::Window* gained_active,
      aura::Window* lost_active) override;

  // Overridden from keyboard::KeyboardControllerObserver:
  void OnKeyboardBoundsChanging(const gfx::Rect& new_bounds) override;

  // Overridden from ash::LockStateObserver:
  void OnLockStateEvent(LockStateObserver::EventType event) override;

  // Overridden from ash::SessionStateObserver:
  void SessionStateChanged(SessionStateDelegate::SessionState state) override;

  // TODO(msw): Remove these accessors, kept temporarily to simplify changes.
  ShelfAlignment GetAlignment() const { return shelf_widget_->GetAlignment(); }

  // TODO(harrym|oshima): These templates will be moved to a new Shelf class.
  // A helper function for choosing values specific to a shelf alignment.
  template <typename T>
  T SelectValueForShelfAlignment(T bottom, T left, T right) const {
    switch (GetAlignment()) {
      case SHELF_ALIGNMENT_BOTTOM:
      case SHELF_ALIGNMENT_BOTTOM_LOCKED:
        return bottom;
      case SHELF_ALIGNMENT_LEFT:
        return left;
      case SHELF_ALIGNMENT_RIGHT:
        return right;
    }
    NOTREACHED();
    return right;
  }

  template <typename T>
  T PrimaryAxisValue(T horizontal, T vertical) const {
    return IsHorizontalAlignment() ? horizontal : vertical;
  }

  // Is the shelf's alignment horizontal?
  bool IsHorizontalAlignment() const;

  // Set the height of the ChromeVox panel, which takes away space from the
  // available work area from the top of the screen.
  void SetChromeVoxPanelHeight(int height);

 private:
  class AutoHideEventFilter;
  class RootWindowControllerObserverImpl;
  class UpdateShelfObserver;
  friend class AshPopupAlignmentDelegateTest;
  friend class ash::ScreenAsh;
  friend class PanelLayoutManagerTest;
  friend class ShelfLayoutManagerTest;
  friend class ToastManagerTest;
  FRIEND_TEST_ALL_PREFIXES(ash::AshPopupAlignmentDelegateTest, AutoHide);
  FRIEND_TEST_ALL_PREFIXES(ash::WebNotificationTrayTest, PopupAndFullscreen);

  struct TargetBounds {
    TargetBounds();
    ~TargetBounds();

    float opacity;
    float status_opacity;
    gfx::Rect shelf_bounds_in_root;
    gfx::Rect shelf_bounds_in_shelf;
    gfx::Rect status_bounds_in_shelf;
    gfx::Insets work_area_insets;
  };

  struct State {
    State()
        : visibility_state(SHELF_VISIBLE),
          auto_hide_state(SHELF_AUTO_HIDE_HIDDEN),
          window_state(wm::WORKSPACE_WINDOW_STATE_DEFAULT),
          is_screen_locked(false),
          is_adding_user_screen(false) {}

    // Returns true if the two states are considered equal. As
    // |auto_hide_state| only matters if |visibility_state| is
    // |SHELF_AUTO_HIDE|, Equals() ignores the |auto_hide_state| as
    // appropriate.
    bool Equals(const State& other) const {
      return other.visibility_state == visibility_state &&
             (visibility_state != SHELF_AUTO_HIDE ||
              other.auto_hide_state == auto_hide_state) &&
             other.window_state == window_state &&
             other.is_screen_locked == is_screen_locked &&
             other.is_adding_user_screen == is_adding_user_screen;
    }

    ShelfVisibilityState visibility_state;
    ShelfAutoHideState auto_hide_state;
    wm::WorkspaceWindowState window_state;
    bool is_screen_locked;
    bool is_adding_user_screen;
  };

  // Sets the visibility of the shelf to |state|.
  void SetState(ShelfVisibilityState visibility_state);

  // Updates the bounds and opacity of the shelf and status widgets.
  // If |observer| is specified, it will be called back when the animations, if
  // any, are complete.
  void UpdateBoundsAndOpacity(const TargetBounds& target_bounds,
                              bool animate,
                              ui::ImplicitAnimationObserver* observer);

  // Stops any animations and progresses them to the end.
  void StopAnimating();

  // Calculates the target bounds assuming visibility of |visible|.
  void CalculateTargetBounds(const State& state, TargetBounds* target_bounds);

  // Updates the target bounds if a gesture-drag is in progress. This is only
  // used by |CalculateTargetBounds()|.
  void UpdateTargetBoundsForGesture(TargetBounds* target_bounds) const;

  // Updates the background of the shelf.
  void UpdateShelfBackground(BackgroundAnimatorChangeType type);

  // Returns how the shelf background is painted.
  ShelfBackgroundType GetShelfBackgroundType() const;

  // Updates the auto hide state immediately.
  void UpdateAutoHideStateNow();

  // Stops the auto hide timer and clears
  // |mouse_over_shelf_when_auto_hide_timer_started_|.
  void StopAutoHideTimer();

  // Returns the bounds of an additional region which can trigger showing the
  // shelf. This region exists to make it easier to trigger showing the shelf
  // when the shelf is auto hidden and the shelf is on the boundary between
  // two displays.
  gfx::Rect GetAutoHideShowShelfRegionInScreen() const;

  // Returns the AutoHideState. This value is determined from the shelf and
  // tray.
  ShelfAutoHideState CalculateAutoHideState(
      ShelfVisibilityState visibility_state) const;

  // Returns true if |window| is a descendant of the shelf.
  bool IsShelfWindow(aura::Window* window);

  int GetWorkAreaInsets(const State& state, int size) const;

  // Overridden from DockedWindowLayoutManagerObserver:
  void OnDockBoundsChanging(
      const gfx::Rect& dock_bounds,
      DockedWindowLayoutManagerObserver::Reason reason) override;

  // Called when the LoginUI changes from visible to invisible.
  void UpdateShelfVisibilityAfterLoginUIChange();

  // Compute |target_bounds| opacity based on gesture and shelf visibility.
  float ComputeTargetOpacity(const State& state);

  // The RootWindow is cached so that we don't invoke Shell::GetInstance() from
  // our destructor. We avoid that as at the time we're deleted Shell is being
  // deleted too.
  aura::Window* root_window_;

  ash::wm::WindowState::FullscreenShelfMode GetShelfModeForFullscreen() const;

  int GetShelfInsetsForAutoHide() const;

  // True when inside UpdateBoundsAndOpacity() method. Used to prevent calling
  // UpdateBoundsAndOpacity() again from SetChildBounds().
  bool updating_bounds_;

  bool in_shutdown_ = false;

  // Current state.
  State state_;

  ShelfWidget* shelf_widget_;

  WorkspaceController* workspace_controller_;

  // Do any windows overlap the shelf? This is maintained by WorkspaceManager.
  bool window_overlaps_shelf_;

  base::OneShotTimer auto_hide_timer_;

  // Whether the mouse was over the shelf when the auto hide timer started.
  // False when neither the auto hide timer nor the timer task are running.
  bool mouse_over_shelf_when_auto_hide_timer_started_;

  // EventFilter used to detect when user moves the mouse over the shelf to
  // trigger showing the shelf. Used in classic ash.
  std::unique_ptr<AutoHideEventFilter> auto_hide_event_filter_;

  // EventFilter used to detect when user issues a gesture on a bezel sensor.
  std::unique_ptr<ShelfBezelEventFilter> bezel_event_filter_;

  base::ObserverList<ShelfLayoutManagerObserver> observers_;

  ShelfGestureHandler gesture_handler_;

  // The shelf reacts to gesture-drags, and can be set to auto-hide for certain
  // gestures. Some shelf behaviour (e.g. visibility state, background color
  // etc.) are affected by various stages of the drag. The enum keeps track of
  // the present status of the gesture drag.
  enum GestureDragStatus {
    GESTURE_DRAG_NONE,
    GESTURE_DRAG_IN_PROGRESS,
    GESTURE_DRAG_CANCEL_IN_PROGRESS,
    GESTURE_DRAG_COMPLETE_IN_PROGRESS
  };
  GestureDragStatus gesture_drag_status_;

  // Tracks the amount of the drag. The value is only valid when
  // |gesture_drag_status_| is set to GESTURE_DRAG_IN_PROGRESS.
  float gesture_drag_amount_;

  // Manage the auto-hide state during the gesture.
  ShelfAutoHideState gesture_drag_auto_hide_state_;

  // Used to delay updating shelf background.
  UpdateShelfObserver* update_shelf_observer_;

  // The bounds of the keyboard.
  gfx::Rect keyboard_bounds_;

  // The bounds of the dock.
  gfx::Rect dock_bounds_;

  // The bounds within the root window not occupied by the shelf nor the virtual
  // keyboard.
  gfx::Rect user_work_area_bounds_;

  // The height of the ChromeVox panel at the top of the screen, which
  // needs to be removed from the available work area.
  int chromevox_panel_height_;

  // The show hide animation duration override or 0 for default.
  int duration_override_in_ms_;

  // The flag to enforce invisible shelf (as in MD-experiemntal).
  // TODO(oshima): Remove this when MD immersive is launched.
  bool invisible_auto_hide_shelf_ = false;

  std::unique_ptr<RootWindowControllerObserverImpl>
      root_window_controller_observer_;

  DISALLOW_COPY_AND_ASSIGN(ShelfLayoutManager);
};

}  // namespace ash

#endif  // ASH_SHELF_SHELF_LAYOUT_MANAGER_H_
