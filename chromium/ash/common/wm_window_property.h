// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMMON_WM_WINDOW_PROPERTY_H_
#define ASH_COMMON_WM_WINDOW_PROPERTY_H_

namespace ash {

enum class WmWindowProperty {
  // Not a valid property; used for property key translation purposes.
  INVALID_PROPERTY,

  // Type bool.
  ALWAYS_ON_TOP,

  // Type bool. See aura::client:kExcludeFromMruKey for details.
  EXCLUDE_FROM_MRU,

  // Type int, but cast to ui::ModalType.
  MODAL_TYPE,

  // Type int.
  SHELF_ICON_RESOURCE_ID,

  // Type int.
  SHELF_ID,

  // Type int, but cast to ShelfItemType.
  SHELF_ITEM_TYPE,

  // Type bool.
  SNAP_CHILDREN_TO_PIXEL_BOUNDARY,

  // Type SkColor. See aura::client::kTopViewColor for details.
  TOP_VIEW_COLOR,

  // Type int. See aura::client::kTopViewInset for details.
  TOP_VIEW_INSET,
};

}  // namespace ash

#endif  // ASH_COMMON_WM_WINDOW_PROPERTY_H_
