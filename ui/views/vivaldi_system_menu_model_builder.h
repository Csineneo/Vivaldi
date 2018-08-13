// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved.
//
// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_VIVALDI_SYSTEM_MENU_MODEL_BUILDER_H_
#define UI_VIEWS_VIVALDI_SYSTEM_MENU_MODEL_BUILDER_H_

#include <memory>

#include "base/macros.h"
#include "chrome/browser/ui/views/frame/system_menu_model_delegate.h"

class Browser;
class ZoomMenuModel;

namespace ui {
class AcceleratorProvider;
class MenuModel;
class SimpleMenuModel;
}

// VivaldiSystemMenuModelBuilder is responsible for building and owning the
// system menu model.
class VivaldiSystemMenuModelBuilder {
 public:
  VivaldiSystemMenuModelBuilder(ui::AcceleratorProvider* provider,
                                Browser* browser);
  ~VivaldiSystemMenuModelBuilder();

  // Populates the menu.
  void Init();

  // Returns the menu model. VivaldiSystemMenuModelBuilder owns the returned
  // model.
  ui::MenuModel* menu_model() { return menu_model_.get(); }

 private:
  Browser* browser() { return menu_delegate_.browser(); }

  // Populates |model| with the appropriate contents.
  void BuildMenu(ui::SimpleMenuModel* model);
  void BuildSystemMenuForBrowserWindow(ui::SimpleMenuModel* model);
  void BuildSystemMenuForAppOrPopupWindow(ui::SimpleMenuModel* model);

  // Adds items for toggling the frame type (if necessary).
  void AddFrameToggleItems(ui::SimpleMenuModel* model);

  SystemMenuModelDelegate menu_delegate_;
  std::unique_ptr<ui::MenuModel> menu_model_;
  std::unique_ptr<ZoomMenuModel> zoom_menu_contents_;

  DISALLOW_COPY_AND_ASSIGN(VivaldiSystemMenuModelBuilder);
};

#endif  // UI_VIEWS_VIVALDI_SYSTEM_MENU_MODEL_BUILDER_H_
