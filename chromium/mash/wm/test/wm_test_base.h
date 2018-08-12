// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MASH_WM_TEST_WM_TEST_BASE_H_
#define MASH_WM_TEST_WM_TEST_BASE_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/wm/public/window_types.h"

namespace display {
class Display;
}

namespace gfx {
class Rect;
}

namespace mus {
class Window;
}

namespace mash {
namespace wm {

class RootWindowController;
class WmTestHelper;

// Base class for window manager tests that want to configure
// WindowTreeConnection without a connection to mus.
class WmTestBase : public testing::Test {
 public:
  WmTestBase();
  ~WmTestBase() override;

  // TODO(sky): temporary until http://crbug.com/611563 is fixed.
  bool SupportsMultipleDisplays() const;

  // Update the display configuration as given in |display_spec|.
  // See ash::test::DisplayManagerTestApi::UpdateDisplay for more details.
  void UpdateDisplay(const std::string& display_spec);

  mus::Window* GetPrimaryRootWindow();
  mus::Window* GetSecondaryRootWindow();

  display::Display GetPrimaryDisplay();
  display::Display GetSecondaryDisplay();

  // Creates a top level window visible window in the appropriate container.
  // NOTE: you can explicitly destroy the returned value if necessary, but it
  // will also be automatically destroyed when the WindowTreeConnection is
  // destroyed.
  mus::Window* CreateTestWindow(const gfx::Rect& bounds);
  mus::Window* CreateTestWindow(const gfx::Rect& bounds,
                                ui::wm::WindowType window_type);

  // Creates a window parented to |parent|. The returned window is visible.
  mus::Window* CreateChildTestWindow(mus::Window* parent,
                                     const gfx::Rect& bounds);

 protected:
  // testing::Test:
  void SetUp() override;
  void TearDown() override;

 private:
  // Returns the RootWindowControllers ordered by display id (which we assume
  // correlates with creation order).
  std::vector<RootWindowController*> GetRootsOrderedByDisplayId();

  bool setup_called_ = false;
  bool teardown_called_ = false;
  std::unique_ptr<WmTestHelper> test_helper_;

  DISALLOW_COPY_AND_ASSIGN(WmTestBase);
};

}  // namespace wm
}  // namespace mash

#endif  // MASH_WM_TEST_WM_TEST_BASE_H_
