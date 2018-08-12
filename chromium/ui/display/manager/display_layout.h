// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_MANAGER_DISPLAY_LAYOUT_H_
#define UI_DISPLAY_MANAGER_DISPLAY_LAYOUT_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_piece.h"
#include "ui/display/display_export.h"

namespace base {
class Value;
template <typename T> class JSONValueConverter;
}

namespace gfx {
class Display;
}

namespace display {

// An identifier used to manage display layout in DisplayManager /
// DisplayLayoutStore.
using DisplayIdList = std::vector<int64_t>;

using DisplayList = std::vector<gfx::Display>;

// DisplayPlacement specifies where the display (D) is placed relative
// to parent (P) display.  In the following example, the display (D)
// given by |display_id| is placed at the left side of the parent
// display (P) given by |parent_display_id|, with a negative offset.
//
//        +      +--------+
// offset |      |        |
//        +      |   D    +--------+
//               |        |        |
//               +--------+   P    |
//                        |        |
//                        +--------+
//
struct DISPLAY_EXPORT DisplayPlacement {
  // The id of the display this placement will be applied to.
  int64_t display_id;

  // The parent display id to which the above display is placed.
  int64_t parent_display_id;

  // To which side the parent display the display is positioned.
  enum Position { TOP, RIGHT, BOTTOM, LEFT };
  Position position;

  // The offset of the position of the secondary display. The offset is
  // based on the top/left edge of the primary display.
  int offset;

  DisplayPlacement(Position position, int offset);
  DisplayPlacement();
  DisplayPlacement(const DisplayPlacement& placement);

  DisplayPlacement& Swap();

  std::string ToString() const;

  static std::string PositionToString(Position position);
  static bool StringToPosition(const base::StringPiece& string,
                               Position* position);
};

class DISPLAY_EXPORT DisplayLayout final {
 public:
  DisplayLayout();
  ~DisplayLayout();

  // Validates the layout object.
  static bool Validate(const DisplayIdList& list, const DisplayLayout& layout);

  std::vector<DisplayPlacement> placement_list;

  // True if displays are mirrored.
  bool mirrored;

  // True if multi displays should default to unified mode.
  bool default_unified;

  // The id of the display used as a primary display.
  int64_t primary_id;

  scoped_ptr<DisplayLayout> Copy() const;

  // Test if the |layout| has the same placement list. Other fields such
  // as mirrored, primary_id are ignored.
  bool HasSamePlacementList(const DisplayLayout& layout) const;

  // Returns string representation of the layout for debugging/testing.
  std::string ToString() const;

  // Returns the DisplayPlacement entry matching |display_id| if it exists,
  // otherwise returns a DisplayPlacement with an invalid display id.
  DisplayPlacement FindPlacementById(int64_t display_id) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(DisplayLayout);
};

}  // namespace display

#endif  // UI_DISPLAY_MANAGER_DISPLAY_LAYOUT_H_
