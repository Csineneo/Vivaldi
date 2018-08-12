// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/gl/test/gl_surface_test_support.h"

namespace {

class AppListShowerTestSuite : public base::TestSuite {
 public:
  AppListShowerTestSuite(int argc, char** argv) : base::TestSuite(argc, argv) {}

 protected:
  void Initialize() override {
    gfx::GLSurfaceTestSupport::InitializeOneOff();
    base::TestSuite::Initialize();
    ui::RegisterPathProvider();

    base::FilePath ui_test_pak_path;
    ASSERT_TRUE(PathService::Get(ui::UI_TEST_PAK, &ui_test_pak_path));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(ui_test_pak_path);
  }

  void Shutdown() override {
    ui::ResourceBundle::CleanupSharedInstance();
    base::TestSuite::Shutdown();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AppListShowerTestSuite);
};

}  // namespace

int main(int argc, char** argv) {
  AppListShowerTestSuite test_suite(argc, argv);

  return base::LaunchUnitTests(
      argc, argv,
      base::Bind(&AppListShowerTestSuite::Run, base::Unretained(&test_suite)));
}
