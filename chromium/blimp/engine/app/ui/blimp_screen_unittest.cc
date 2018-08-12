// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/engine/app/ui/blimp_screen.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/stringprintf.h"
#include "blimp/engine/app/switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/display.h"
#include "ui/gfx/display_observer.h"
#include "ui/gfx/screen.h"

using testing::InSequence;

namespace blimp {
namespace engine {
namespace {

// Checks if two gfx::Displays have the ID.
MATCHER_P(EqualsDisplay, display, "") {
  return display.id() == arg.id();
}

class MockDisplayObserver : public gfx::DisplayObserver {
 public:
  MockDisplayObserver() {}
  ~MockDisplayObserver() override {}

  MOCK_METHOD1(OnDisplayAdded, void(const gfx::Display&));
  MOCK_METHOD1(OnDisplayRemoved, void(const gfx::Display&));
  MOCK_METHOD2(OnDisplayMetricsChanged,
               void(const gfx::Display& display, uint32_t changed_metrics));
};

class BlimpScreenTest : public testing::Test {
 protected:
  void SetUp() override {
    screen_ = make_scoped_ptr(new BlimpScreen);
    screen_->AddObserver(&observer1_);
    screen_->AddObserver(&observer2_);
  }

  scoped_ptr<BlimpScreen> screen_;
  testing::StrictMock<MockDisplayObserver> observer1_;
  testing::StrictMock<MockDisplayObserver> observer2_;
};

TEST_F(BlimpScreenTest, ObserversAreInfomed) {
  auto display = screen_->GetPrimaryDisplay();
  uint32_t changed_metrics =
      gfx::DisplayObserver::DISPLAY_METRIC_DEVICE_SCALE_FACTOR |
      gfx::DisplayObserver::DISPLAY_METRIC_BOUNDS;

  InSequence s;
  EXPECT_CALL(observer1_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));
  EXPECT_CALL(observer2_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));

  changed_metrics = gfx::DisplayObserver::DISPLAY_METRIC_BOUNDS;
  EXPECT_CALL(observer1_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));
  EXPECT_CALL(observer2_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));

  changed_metrics = gfx::DisplayObserver::DISPLAY_METRIC_DEVICE_SCALE_FACTOR;
  EXPECT_CALL(observer1_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));
  EXPECT_CALL(observer2_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));

  gfx::Size size1(100, 200);
  screen_->UpdateDisplayScaleAndSize(2.0f, size1);
  EXPECT_EQ(size1, screen_->GetPrimaryDisplay().GetSizeInPixel());
  EXPECT_EQ(2.0f, screen_->GetPrimaryDisplay().device_scale_factor());

  screen_->UpdateDisplayScaleAndSize(2.0f, size1);

  gfx::Size size2(200, 100);
  screen_->UpdateDisplayScaleAndSize(2.0f, size2);
  EXPECT_EQ(size2, screen_->GetPrimaryDisplay().GetSizeInPixel());
  EXPECT_EQ(2.0f, screen_->GetPrimaryDisplay().device_scale_factor());

  screen_->UpdateDisplayScaleAndSize(3.0f, size2);
  EXPECT_EQ(3.0f, screen_->GetPrimaryDisplay().device_scale_factor());
}

TEST_F(BlimpScreenTest, RemoveObserver) {
  screen_->RemoveObserver(&observer2_);
  auto display = screen_->GetPrimaryDisplay();
  uint32_t changed_metrics =
      gfx::DisplayObserver::DISPLAY_METRIC_DEVICE_SCALE_FACTOR |
      gfx::DisplayObserver::DISPLAY_METRIC_BOUNDS;
  EXPECT_CALL(observer1_,
              OnDisplayMetricsChanged(EqualsDisplay(display), changed_metrics));

  gfx::Size size1(100, 100);
  screen_->UpdateDisplayScaleAndSize(2.0f, size1);
  EXPECT_EQ(size1, screen_->GetPrimaryDisplay().GetSizeInPixel());
}

}  // namespace
}  // namespace engine
}  // namespace blimp
