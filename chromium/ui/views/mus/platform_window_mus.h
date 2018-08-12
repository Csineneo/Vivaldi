// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_MUS_PLATFORM_WINDOW_MUS_H_
#define UI_VIEWS_MUS_PLATFORM_WINDOW_MUS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/macros.h"
#include "components/mus/public/cpp/window_observer.h"
#include "ui/platform_window/platform_window.h"
#include "ui/views/mus/mus_export.h"

namespace bitmap_uploader {
class BitmapUploader;
}

namespace shell {
class Connector;
}

namespace ui {
class ViewProp;
}

namespace views {

// This class has been marked for deletion. Its implementation is being rolled
// into views::NativeWidgetMus. See crbug.com/609555 for details.
class VIEWS_MUS_EXPORT PlatformWindowMus
    : public NON_EXPORTED_BASE(ui::PlatformWindow) {
 public:
  PlatformWindowMus(ui::PlatformWindowDelegate* delegate,
                    shell::Connector* connector,
                    mus::Window* mus_window);
  ~PlatformWindowMus() override;

  // ui::PlatformWindow:
  void Show() override;
  void Hide() override;
  void Close() override;
  void SetBounds(const gfx::Rect& bounds) override;
  gfx::Rect GetBounds() override;
  void SetTitle(const base::string16& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  void ToggleFullscreen() override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  void SetCursor(ui::PlatformCursor cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  ui::PlatformImeController* GetPlatformImeController() override;

 private:
  friend class PlatformWindowMusTest;

  ui::PlatformWindowDelegate* delegate_;
  mus::Window* mus_window_;

  // True if OnWindowDestroyed() has been received.
  bool mus_window_destroyed_;

  std::unique_ptr<bitmap_uploader::BitmapUploader> bitmap_uploader_;
  std::unique_ptr<ui::ViewProp> prop_;
#ifndef NDEBUG
  std::unique_ptr<base::WeakPtrFactory<PlatformWindowMus>> weak_factory_;
#endif

  DISALLOW_COPY_AND_ASSIGN(PlatformWindowMus);
};

}  // namespace views

#endif  // UI_VIEWS_MUS_PLATFORM_WINDOW_MUS_H_
