// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGPhysicalFragment_h
#define NGPhysicalFragment_h

#include "core/CoreExport.h"
#include "core/layout/ng/ng_physical_fragment_base.h"
#include "core/layout/ng/ng_units.h"
#include "platform/heap/Handle.h"
#include "wtf/Vector.h"

namespace blink {

class CORE_EXPORT NGPhysicalFragment final : public NGPhysicalFragmentBase {
 public:
  // This modifies the passed-in children vector.
  NGPhysicalFragment(NGPhysicalSize size,
                     NGPhysicalSize overflow,
                     HeapVector<Member<const NGPhysicalFragmentBase>>& children,
                     NGMarginStrut margin_strut)
      : NGPhysicalFragmentBase(size, overflow, FragmentBox, margin_strut) {
    children_.swap(children);
  }

  const HeapVector<Member<const NGPhysicalFragmentBase>>& Children() const {
    return children_;
  }

  DEFINE_INLINE_TRACE_AFTER_DISPATCH() {
    visitor->trace(children_);
    NGPhysicalFragmentBase::traceAfterDispatch(visitor);
  }

 private:
  HeapVector<Member<const NGPhysicalFragmentBase>> children_;
};

WILL_NOT_BE_EAGERLY_TRACED_CLASS(NGPhysicalFragment);

}  // namespace blink

#endif  // NGPhysicalFragment_h
