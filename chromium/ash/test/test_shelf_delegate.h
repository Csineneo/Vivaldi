// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_TEST_SHELF_DELEGATE_H_
#define ASH_TEST_TEST_SHELF_DELEGATE_H_

#include <memory>
#include <set>
#include <string>

#include "ash/shelf/shelf_delegate.h"
#include "base/macros.h"
#include "ui/aura/window_observer.h"

namespace ash {

class WmWindow;

namespace test {

class ShelfInitializer;

// Test implementation of ShelfDelegate.
// Tests may create icons for windows by calling AddShelfItem().
class TestShelfDelegate : public ShelfDelegate, public aura::WindowObserver {
 public:
  TestShelfDelegate();
  ~TestShelfDelegate() override;

  // Adds a ShelfItem for the given |window|. The ShelfItem's status will be
  // STATUS_CLOSED.
  void AddShelfItem(WmWindow* window);

  // Adds a ShelfItem for the given |window| and |app_id|. The ShelfItem's
  // status will be STATUS_CLOSED.
  void AddShelfItem(WmWindow* window, const std::string& app_id);

  // Removes the ShelfItem for the specified |window| and unpins it if it was
  // pinned. The |window|'s ShelfID to app id mapping will be removed if it
  // exists.
  void RemoveShelfItemForWindow(WmWindow* window);

  static TestShelfDelegate* instance() { return instance_; }

  // WindowObserver implementation
  void OnWindowDestroying(aura::Window* window) override;
  void OnWindowHierarchyChanging(const HierarchyChangeParams& params) override;

  // ShelfDelegate implementation.
  ShelfID GetShelfIDForAppID(const std::string& app_id) override;
  ShelfID GetShelfIDForAppIDAndLaunchID(const std::string& app_id,
                                        const std::string& launch_id) override;
  const std::string& GetAppIDForShelfID(ShelfID id) override;
  void PinAppWithID(const std::string& app_id) override;
  bool IsAppPinned(const std::string& app_id) override;
  void UnpinAppWithID(const std::string& app_id) override;

 private:
  static TestShelfDelegate* instance_;

  std::unique_ptr<ShelfInitializer> shelf_initializer_;

  std::set<std::string> pinned_apps_;

  DISALLOW_COPY_AND_ASSIGN(TestShelfDelegate);
};

}  // namespace test
}  // namespace ash

#endif  // ASH_TEST_TEST_SHELF_DELEGATE_H_
