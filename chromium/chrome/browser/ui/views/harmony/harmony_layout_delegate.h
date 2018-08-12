// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_HARMONY_HARMONY_LAYOUT_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_HARMONY_HARMONY_LAYOUT_DELEGATE_H_

#include "chrome/browser/ui/views/harmony/layout_delegate.h"

class HarmonyLayoutDelegate : public LayoutDelegate {
 public:
  HarmonyLayoutDelegate() {}
  ~HarmonyLayoutDelegate() override {}

  // Returns the singleton HarmonyLayoutDelegate instance.
  static HarmonyLayoutDelegate* Get();

  // views::LayoutDelegate:
  int GetLayoutDistance(LayoutDistanceType type) const override;
  bool UseExtraDialogPadding() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(HarmonyLayoutDelegate);
};

#endif  // CHROME_BROWSER_UI_VIEWS_HARMONY_HARMONY_LAYOUT_DELEGATE_H_
