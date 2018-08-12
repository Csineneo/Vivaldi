// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_PUBLIC_PROVIDER_CHROME_BROWSER_TEST_CHROME_BROWSER_PROVIDER_H_
#define IOS_PUBLIC_PROVIDER_CHROME_BROWSER_TEST_CHROME_BROWSER_PROVIDER_H_

#include <memory>

#include "base/macros.h"
#include "ios/public/provider/chrome/browser/chrome_browser_provider.h"
#include "ios/public/provider/chrome/browser/ui/text_field_styling.h"

namespace ios {

class TestUpdatableResourceProvider;

class TestChromeBrowserProvider : public ChromeBrowserProvider {
 public:
  TestChromeBrowserProvider();
  ~TestChromeBrowserProvider() override;

  // Returns the current provider as a |TestChromeBrowserProvider|.
  static TestChromeBrowserProvider* GetTestProvider();

  // ChromeBrowserProvider:
  void SetChromeIdentityServiceForTesting(
      std::unique_ptr<ChromeIdentityService> service) override;
  ChromeIdentityService* GetChromeIdentityService() override;
  UpdatableResourceProvider* GetUpdatableResourceProvider() override;
  UITextField<TextFieldStyling>* CreateStyledTextField(
      CGRect frame) const override NS_RETURNS_RETAINED;

 private:
  std::unique_ptr<ChromeIdentityService> chrome_identity_service_;
  std::unique_ptr<TestUpdatableResourceProvider>
      test_updatable_resource_provider_;

  DISALLOW_COPY_AND_ASSIGN(TestChromeBrowserProvider);
};

}  // namespace ios

#endif  // IOS_PUBLIC_PROVIDER_CHROME_BROWSER_TEST_CHROME_BROWSER_PROVIDER_H_
