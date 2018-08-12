// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_ANIMATION_PROPERTY_ANIMATION_STATE_H_
#define CC_ANIMATION_PROPERTY_ANIMATION_STATE_H_

#include "cc/animation/target_property.h"
#include "cc/base/cc_export.h"

namespace cc {

struct CC_EXPORT PropertyAnimationState {
  PropertyAnimationState();
  PropertyAnimationState(const PropertyAnimationState& rhs);
  ~PropertyAnimationState();

  TargetProperties currently_running;
  TargetProperties potentially_animating;

  bool operator==(const PropertyAnimationState& other) const;
  bool operator!=(const PropertyAnimationState& other) const;

  PropertyAnimationState& operator|=(const PropertyAnimationState& other);
  PropertyAnimationState& operator^=(const PropertyAnimationState& other);
  PropertyAnimationState& operator&=(const PropertyAnimationState& other);

  bool IsValid() const;

  void Clear();
};

PropertyAnimationState operator^(const PropertyAnimationState& lhs,
                                 const PropertyAnimationState& rhs);

}  // namespace cc

#endif  // CC_ANIMATION_PROPERTY_ANIMATION_STATE_H_
