// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_STYLE_PLATFORM_STYLE_H_
#define UI_VIEWS_STYLE_PLATFORM_STYLE_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/combobox/combobox.h"

namespace views {

class Border;
class FocusableBorder;
class LabelButton;
class LabelButtonBorder;
class ScrollBar;

// Cross-platform API for providing platform-specific styling for toolkit-views.
class PlatformStyle {
 public:
  // Creates an ImageSkia containing the image to use for the combobox arrow.
  // The |is_enabled| argument is true if the control the arrow is for is
  // enabled, and false if the control is disabled. The |style| argument is the
  // style of the combobox the arrow is being drawn for.
  static gfx::ImageSkia CreateComboboxArrow(bool is_enabled,
                                            Combobox::Style style);

  // Creates the appropriate border for a focusable Combobox.
  static scoped_ptr<FocusableBorder> CreateComboboxBorder();

  // Creates the appropriate background for a Combobox.
  static scoped_ptr<Background> CreateComboboxBackground();

  // Creates the default label button border for the given |style|. Used when a
  // custom default border is not provided for a particular LabelButton class.
  static scoped_ptr<LabelButtonBorder> CreateLabelButtonBorder(
      Button::ButtonStyle style);

  // Applies the current system theme to the default border created by |button|.
  static scoped_ptr<Border> CreateThemedLabelButtonBorder(LabelButton* button);

  // Creates the default scrollbar for the given orientation.
  static scoped_ptr<ScrollBar> CreateScrollBar(bool is_horizontal);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(PlatformStyle);
};

}  // namespace views

#endif  // UI_VIEWS_STYLE_PLATFORM_STYLE_H_
