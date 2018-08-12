// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Interpolation_h
#define Interpolation_h

#include <memory>
#include "core/CoreExport.h"
#include "core/animation/InterpolableValue.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/RefCounted.h"

namespace blink {

class PropertyHandle;

// Represents an animation's effect between an adjacent pair of
// PropertySpecificKeyframes.
class CORE_EXPORT Interpolation : public RefCounted<Interpolation> {
  WTF_MAKE_NONCOPYABLE(Interpolation);

 public:
  virtual ~Interpolation() {}

  virtual void Interpolate(int iteration, double fraction) = 0;

  virtual bool IsInvalidatableInterpolation() const { return false; }
  virtual bool IsLegacyStyleInterpolation() const { return false; }
  virtual bool IsTransitionInterpolation() const { return false; }

  virtual const PropertyHandle& GetProperty() const = 0;
  virtual bool DependsOnUnderlyingValue() const { return false; }

 protected:
  Interpolation() {}
};

using ActiveInterpolations = Vector<RefPtr<Interpolation>, 1>;

}  // namespace blink

#endif  // Interpolation_h
