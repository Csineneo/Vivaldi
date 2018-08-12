// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_DRAGDROP_DRAG_UTILS_H_
#define UI_BASE_DRAGDROP_DRAG_UTILS_H_

#include "ui/base/ui_base_export.h"

namespace gfx {
class ImageSkia;
class Vector2d;
}

namespace ui {
class OSExchangeData;
}

namespace drag_utils {

// Sets the drag image on data_object from the supplied ImageSkia.
// |cursor_offset| gives the location of the hotspot for the drag image.
UI_BASE_EXPORT void SetDragImageOnDataObject(const gfx::ImageSkia& image_skia,
                                             const gfx::Vector2d& cursor_offset,
                                             ui::OSExchangeData* data_object);

}  // namespace drag_utils

#endif  // UI_BASE_DRAGDROP_DRAG_UTILS_H_
