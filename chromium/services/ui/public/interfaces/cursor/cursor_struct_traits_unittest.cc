// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/public/interfaces/cursor/cursor_struct_traits.h"

#include "base/message_loop/message_loop.h"
#include "mojo/common/common_custom_types_struct_traits.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/ui/public/interfaces/cursor/cursor.mojom.h"
#include "skia/public/interfaces/bitmap_array_struct_traits.h"
#include "skia/public/interfaces/bitmap_skbitmap_struct_traits.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/cursor_data.h"
#include "ui/gfx/geometry/mojo/geometry_struct_traits.h"

namespace ui {

namespace {

class CursorStructTraitsTest : public testing::Test {
 public:
  CursorStructTraitsTest() {}

 protected:
  bool EchoCursorData(const ui::CursorData& in, ui::CursorData* out) {
    return mojom::CursorData::Deserialize(mojom::CursorData::Serialize(&in),
                                          out);
  }

  DISALLOW_COPY_AND_ASSIGN(CursorStructTraitsTest);
};

std::vector<SkBitmap> CreateTestCursorFrames(const gfx::Size& size,
                                             unsigned int count) {
  std::vector<SkBitmap> frames(count);
  for (size_t i = 0; i < count; ++i) {
    SkBitmap& bitmap = frames[i];
    bitmap.allocN32Pixels(size.width(), size.height());
    bitmap.eraseColor(SK_ColorRED);
  }
  return frames;
}

}  // namespace

// Tests numeric cursor ids.
TEST_F(CursorStructTraitsTest, TestBuiltIn) {
  for (int i = 0; i < 43; ++i) {
    ui::CursorData input(i);

    ui::CursorData output;
    ASSERT_TRUE(EchoCursorData(input, &output));
    EXPECT_TRUE(output.IsType(i));
  }
}

// Test that we copy cursor bitmaps and metadata across the wire.
TEST_F(CursorStructTraitsTest, TestBitmapCursor) {
  const base::TimeDelta kFrameDelay = base::TimeDelta::FromMilliseconds(15);
  const gfx::Point kHotspot = gfx::Point(5, 2);
  const float kScale = 2.0f;

  ui::CursorData input(kHotspot, CreateTestCursorFrames(gfx::Size(10, 10), 3),
                       kScale, kFrameDelay);

  ui::CursorData output;
  ASSERT_TRUE(EchoCursorData(input, &output));

  EXPECT_EQ(kCursorCustom, output.cursor_type());
  EXPECT_EQ(kScale, output.scale_factor());
  EXPECT_EQ(kFrameDelay, output.frame_delay());
  EXPECT_EQ(kHotspot, output.hotspot_in_pixels());

  // Even if the pixel data is logically the same, expect that it has different
  // generation ids.
  EXPECT_FALSE(output.IsSameAs(input));

  // Make a copy of output. It should be the same as output.
  ui::CursorData copy = output;
  EXPECT_TRUE(copy.IsSameAs(output));

  // But make sure that the pixel data actually is equivalent.
  ASSERT_EQ(input.cursor_frames().size(), output.cursor_frames().size());
  for (size_t f = 0; f < input.cursor_frames().size(); ++f) {
    ASSERT_EQ(input.cursor_frames()[f].width(),
              output.cursor_frames()[f].width());
    ASSERT_EQ(input.cursor_frames()[f].height(),
              output.cursor_frames()[f].height());

    input.cursor_frames()[f].lockPixels();
    output.cursor_frames()[f].lockPixels();
    for (int x = 0; x < input.cursor_frames()[f].width(); ++x) {
      for (int y = 0; y < input.cursor_frames()[f].height(); ++y) {
        EXPECT_EQ(input.cursor_frames()[f].getColor(x, y),
                  output.cursor_frames()[f].getColor(x, y));
      }
    }

    output.cursor_frames()[f].unlockPixels();
    input.cursor_frames()[f].unlockPixels();
  }
}

}  // namespace ui
