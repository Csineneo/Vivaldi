// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_ANIMATION_INK_DROP_ANIMATION_CONTROLLER_IMPL_H_
#define UI_VIEWS_ANIMATION_INK_DROP_ANIMATION_CONTROLLER_IMPL_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/animation/ink_drop_animation_controller.h"
#include "ui/views/animation/ink_drop_animation_observer.h"
#include "ui/views/animation/ink_drop_hover_observer.h"
#include "ui/views/views_export.h"

namespace base {
class Timer;
}  // namespace base

namespace views {
namespace test {
class InkDropAnimationControllerImplTestApi;
}  // namespace test

class InkDropAnimation;
class InkDropHost;
class InkDropHover;
class InkDropAnimationControllerFactoryTest;

// A functional implementation of an InkDropAnimationController.
class VIEWS_EXPORT InkDropAnimationControllerImpl
    : public InkDropAnimationController,
      public InkDropAnimationObserver,
      public InkDropHoverObserver {
 public:
  // Constructs an ink drop controller that will attach the ink drop to the
  // given |ink_drop_host|.
  explicit InkDropAnimationControllerImpl(InkDropHost* ink_drop_host);
  ~InkDropAnimationControllerImpl() override;

  // InkDropAnimationController:
  InkDropState GetTargetInkDropState() const override;
  bool IsVisible() const override;
  void AnimateToState(InkDropState ink_drop_state) override;
  void SnapToActivated() override;
  void SetHovered(bool is_hovered) override;

 private:
  friend class test::InkDropAnimationControllerImplTestApi;

  // Destroys |ink_drop_animation_| if it's targeted to the HIDDEN state.
  void DestroyHiddenTargetedAnimations();

  // Creates a new InkDropAnimation and sets it to |ink_drop_animation_|. If
  // |ink_drop_animation_| wasn't null then it will be destroyed using
  // DestroyInkDropAnimation().
  void CreateInkDropAnimation();

  // Destroys the current |ink_drop_animation_|.
  void DestroyInkDropAnimation();

  // Creates a new InkDropHover and sets it to |hover_|. If |hover_| wasn't null
  // then it will be destroyed using DestroyInkDropHover().
  void CreateInkDropHover();

  // Destroys the current |hover_|.
  void DestroyInkDropHover();

  // Adds the |root_layer_| to the |ink_drop_host_| if it hasn't already been
  // added.
  void AddRootLayerToHostIfNeeded();

  // Removes the |root_layer_| from the |ink_drop_host_| if no ink drop ripple
  // or hover is active.
  void RemoveRootLayerFromHostIfNeeded();

  // Returns true if the hover animation is in the process of fading in or
  // is visible.
  bool IsHoverFadingInOrVisible() const;

  // views::InkDropAnimationObserver:
  void AnimationStarted(InkDropState ink_drop_state) override;
  void AnimationEnded(InkDropState ink_drop_state,
                      InkDropAnimationEndedReason reason) override;

  // views::InkDropHoverObserver:
  void AnimationStarted(InkDropHover::AnimationType animation_type) override;
  void AnimationEnded(InkDropHover::AnimationType animation_type,
                      InkDropAnimationEndedReason reason) override;

  // Enables or disables the hover state based on |is_hovered| and if an
  // animation is triggered it will be scheduled to have the given
  // |animation_duration|. If |explode| is true the hover will expand as it
  // fades out. |explode| is ignored when |is_hovered| is true.
  void SetHoveredInternal(bool is_hovered,
                          base::TimeDelta animation_duration,
                          bool explode);

  // Starts the |hover_after_animation_timer_| timer. This will stop the current
  // |hover_after_animation_timer_| instance if it exists.
  void StartHoverAfterAnimationTimer();

  // Stops and destroys the current |hover_after_animation_timer_| instance.
  void StopHoverAfterAnimationTimer();

  // Callback for when the |hover_after_animation_timer_| fires.
  void HoverAfterAnimationTimerFired();

  // The host of the ink drop. Used to poll for information such as whether the
  // hover should be shown or not.
  InkDropHost* ink_drop_host_;

  // The root Layer that parents the InkDropAnimation layers and the
  // InkDropHover layers. The |root_layer_| is the one that is added and removed
  // from the InkDropHost.
  scoped_ptr<ui::Layer> root_layer_;

  // True when the |root_layer_| has been added to the |ink_drop_host_|.
  bool root_layer_added_to_host_;

  // The current InkDropHover. Lazily created using CreateInkDropHover();
  scoped_ptr<InkDropHover> hover_;

  // Tracks the logical hovered state of |this| as manipulated by the public
  // SetHovered() function.
  bool is_hovered_;

  // The current InkDropAnimation. Created on demand using
  // CreateInkDropAnimation().
  scoped_ptr<InkDropAnimation> ink_drop_animation_;

  // The timer used to delay the hover fade in after an ink drop animation.
  scoped_ptr<base::Timer> hover_after_animation_timer_;

  DISALLOW_COPY_AND_ASSIGN(InkDropAnimationControllerImpl);
};

}  // namespace views

#endif  // UI_VIEWS_ANIMATION_INK_DROP_ANIMATION_CONTROLLER_IMPL_H_
