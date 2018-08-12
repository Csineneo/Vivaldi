// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/style/platform_style.h"

#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/label_button_border.h"
#import "ui/views/controls/scrollbar/cocoa_scroll_bar.h"
#include "ui/views/style/mac/dialog_button_border_mac.h"

namespace views {

// static
scoped_ptr<LabelButtonBorder> PlatformStyle::CreateLabelButtonBorder(
    Button::ButtonStyle style) {
  if (style == Button::STYLE_BUTTON)
    return make_scoped_ptr(new DialogButtonBorderMac());

  return make_scoped_ptr(new LabelButtonAssetBorder(style));
}

// static
scoped_ptr<ScrollBar> PlatformStyle::CreateScrollBar(bool is_horizontal) {
  return make_scoped_ptr(new CocoaScrollBar(is_horizontal));
}

}  // namespace views
