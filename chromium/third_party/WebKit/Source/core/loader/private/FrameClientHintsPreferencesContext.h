// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FrameClientHintsPreferencesContext_h
#define FrameClientHintsPreferencesContext_h

#include "core/frame/Frame.h"
#include "platform/heap/Persistent.h"
#include "platform/loader/fetch/ClientHintsPreferences.h"
#include "platform/wtf/Allocator.h"

namespace blink {

class FrameClientHintsPreferencesContext final
    : public ClientHintsPreferences::Context {
  STACK_ALLOCATED();

 public:
  explicit FrameClientHintsPreferencesContext(Frame*);

  void CountClientHintsDPR() override;
  void CountClientHintsResourceWidth() override;
  void CountClientHintsViewportWidth() override;

 private:
  Member<Frame> frame_;
};

}  // namespace blink

#endif  // FrameClientHintsPreferencesContext_h
