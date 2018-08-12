// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGLineBreaker_h
#define NGLineBreaker_h

#include "core/CoreExport.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/text/AtomicString.h"

namespace blink {

class NGInlineLayoutAlgorithm;

// Represents a line breaker.
class CORE_EXPORT NGLineBreaker {
 public:
  NGLineBreaker(const AtomicString locale) : locale_(locale) {}
  ~NGLineBreaker() {}
  STACK_ALLOCATED();

  void BreakLines(NGInlineLayoutAlgorithm*, const String&, unsigned);

 private:
  const AtomicString locale_;
};

}  // namespace blink

#endif  // NGLineBreaker_h
