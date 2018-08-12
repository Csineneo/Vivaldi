// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_ANIMATION_INK_DROP_HOVER_H_
#define UI_VIEWS_ANIMATION_INK_DROP_HOVER_H_

#include <iosfwd>
#include <memory>

#include "base/macros.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/transform.h"
#include "ui/views/views_export.h"

namespace ui {
class Layer;
class CallbackLayerAnimationObserver;
}  // namespace ui

namespace views {
namespace test {
class InkDropHoverTestApi;
}  // namespace test

class RoundedRectangleLayerDelegate;
class InkDropHoverObserver;

// Manages fade in/out animations for a painted Layer that is used to provide
// visual feedback on ui::Views for mouse hover states.
class VIEWS_EXPORT InkDropHover {
 public:
  enum AnimationType { FADE_IN, FADE_OUT };

  InkDropHover(const gfx::Size& size,
               int corner_radius,
               const gfx::Point& center_point,
               SkColor color);
  virtual ~InkDropHover();

  void set_observer(InkDropHoverObserver* observer) { observer_ = observer; }

  void set_explode_size(const gfx::Size& size) { explode_size_ = size; }

  // Returns true if the hover animation is either in the process of fading
  // in or is fully visible.
  bool IsFadingInOrVisible() const;

  // Fades in the hover visual over the given |duration|.
  void FadeIn(const base::TimeDelta& duration);

  // Fades out the hover visual over the given |duration|. If |explode| is true
  // then the hover will animate a size increase in addition to the fade out.
  void FadeOut(const base::TimeDelta& duration, bool explode);

  // The root Layer that can be added in to a Layer tree.
  ui::Layer* layer() { return layer_.get(); }

  // Returns a test api to access internals of this. Default implmentations
  // should return nullptr and test specific subclasses can override to return
  // an instance.
  virtual test::InkDropHoverTestApi* GetTestApi();

 private:
  friend class test::InkDropHoverTestApi;

  // Animates a fade in/out as specified by |animation_type| combined with a
  // transformation from the |initial_size| to the |target_size| over the given
  // |duration|.
  void AnimateFade(AnimationType animation_type,
                   const base::TimeDelta& duration,
                   const gfx::Size& initial_size,
                   const gfx::Size& target_size);

  // Calculates the Transform to apply to |layer_| for the given |size|.
  gfx::Transform CalculateTransform(const gfx::Size& size) const;

  // The callback that will be invoked when a fade in/out animation is started.
  void AnimationStartedCallback(
      AnimationType animation_type,
      const ui::CallbackLayerAnimationObserver& observer);

  // The callback that will be invoked when a fade in/out animation is complete.
  bool AnimationEndedCallback(
      AnimationType animation_type,
      const ui::CallbackLayerAnimationObserver& observer);

  // The size of the hover shape when fully faded in.
  gfx::Size size_;

  // The target size of the hover shape when it expands during a fade out
  // animation.
  gfx::Size explode_size_;

  // The center point of the hover shape in the parent Layer's coordinate space.
  gfx::PointF center_point_;

  // True if the last animation to be initiated was a FADE_IN, and false
  // otherwise.
  bool last_animation_initiated_was_fade_in_;

  // The LayerDelegate that paints the hover |layer_|.
  std::unique_ptr<RoundedRectangleLayerDelegate> layer_delegate_;

  // The visual hover layer that is painted by |layer_delegate_|.
  std::unique_ptr<ui::Layer> layer_;

  InkDropHoverObserver* observer_;

  DISALLOW_COPY_AND_ASSIGN(InkDropHover);
};

// Returns a human readable string for |animation_type|.  Useful for logging.
VIEWS_EXPORT std::string ToString(InkDropHover::AnimationType animation_type);

// This is declared here for use in gtest-based unit tests but is defined in
// the views_test_support target. Depend on that to use this in your unit test.
// This should not be used in production code - call ToString() instead.
void PrintTo(InkDropHover::AnimationType animation_type, ::std::ostream* os);

}  // namespace views

#endif  // UI_VIEWS_ANIMATION_INK_DROP_HOVER_H_
