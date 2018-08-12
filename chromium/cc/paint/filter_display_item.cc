// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/filter_display_item.h"

namespace cc {

FilterDisplayItem::FilterDisplayItem(const FilterOperations& a_filters,
                                     const gfx::RectF& bounds,
                                     const gfx::PointF& origin)
    : DisplayItem(FILTER), bounds(bounds), origin(origin) {
  filters = a_filters;
}

FilterDisplayItem::~FilterDisplayItem() = default;

EndFilterDisplayItem::EndFilterDisplayItem() : DisplayItem(END_FILTER) {}

EndFilterDisplayItem::~EndFilterDisplayItem() = default;

}  // namespace cc
