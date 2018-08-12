// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/api/tabs/tabs_api.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_service_test_base.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/test_browser_window.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/test/test_web_contents_factory.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/test_util.h"

namespace extensions {

namespace {

scoped_ptr<base::ListValue> RunTabsQueryFunction(
    Browser* browser,
    const Extension* extension,
    const std::string& query_info) {
  scoped_refptr<TabsQueryFunction> function(new TabsQueryFunction());
  function->set_extension(extension);
  scoped_ptr<base::Value> value(
      extension_function_test_utils::RunFunctionAndReturnSingleResult(
          function.get(), query_info, browser,
          extension_function_test_utils::NONE));
  return base::ListValue::From(std::move(value));
}

}  // namespace

class TabsApiUnitTest : public ExtensionServiceTestBase {
 protected:
  TabsApiUnitTest() {}
  ~TabsApiUnitTest() override {}

  Browser* browser() { return browser_.get(); }

 private:
  // ExtensionServiceTestBase:
  void SetUp() override;
  void TearDown() override;

  // The browser (and accompanying window).
  scoped_ptr<TestBrowserWindow> browser_window_;
  scoped_ptr<Browser> browser_;

  DISALLOW_COPY_AND_ASSIGN(TabsApiUnitTest);
};

void TabsApiUnitTest::SetUp() {
  ExtensionServiceTestBase::SetUp();
  InitializeEmptyExtensionService();

  browser_window_.reset(new TestBrowserWindow());
  Browser::CreateParams params(profile(), chrome::HOST_DESKTOP_TYPE_NATIVE);
  params.type = Browser::TYPE_TABBED;
  params.window = browser_window_.get();
  browser_.reset(new Browser(params));
}

void TabsApiUnitTest::TearDown() {
  browser_.reset();
  browser_window_.reset();
  ExtensionServiceTestBase::TearDown();
}

TEST_F(TabsApiUnitTest, QueryWithoutTabsPermission) {
  GURL tab_urls[] = {GURL("http://www.google.com"),
                     GURL("http://www.example.com"),
                     GURL("https://www.google.com")};
  std::string tab_titles[] = {"", "Sample title", "Sample title"};

  // Add 3 web contentses to the browser.
  content::TestWebContentsFactory factory;
  content::WebContents* web_contentses[arraysize(tab_urls)];
  for (size_t i = 0; i < arraysize(tab_urls); ++i) {
    content::WebContents* web_contents = factory.CreateWebContents(profile());
    web_contentses[i] = web_contents;
    browser()->tab_strip_model()->AppendWebContents(web_contents, true);
    EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(),
              web_contents);
    content::WebContentsTester* web_contents_tester =
        content::WebContentsTester::For(web_contents);
    web_contents_tester->NavigateAndCommit(tab_urls[i]);
    web_contents->GetController().GetVisibleEntry()->SetTitle(
        base::ASCIIToUTF16(tab_titles[i]));
  }

  const char* kTitleAndURLQueryInfo =
      "[{\"title\": \"Sample title\", \"url\": \"*://www.google.com/*\"}]";

  // An extension without "tabs" permission will see all 3 tabs, because the
  // query_info filter will be ignored.
  scoped_refptr<const Extension> extension = test_util::CreateEmptyExtension();
  scoped_ptr<base::ListValue> tabs_list_without_permission(
      RunTabsQueryFunction(browser(), extension.get(), kTitleAndURLQueryInfo));
  ASSERT_TRUE(tabs_list_without_permission);
  EXPECT_EQ(3u, tabs_list_without_permission->GetSize());

  // An extension with "tabs" permission however will only see the third tab.
  scoped_refptr<const Extension> extension_with_permission =
      ExtensionBuilder()
          .SetManifest(std::move(
              DictionaryBuilder()
                  .Set("name", "Extension with tabs permission")
                  .Set("version", "1.0")
                  .Set("manifest_version", 2)
                  .Set("permissions", std::move(ListBuilder().Append("tabs")))))
          .Build();
  scoped_ptr<base::ListValue> tabs_list_with_permission(RunTabsQueryFunction(
      browser(), extension_with_permission.get(), kTitleAndURLQueryInfo));
  ASSERT_TRUE(tabs_list_with_permission);
  ASSERT_EQ(1u, tabs_list_with_permission->GetSize());

  const base::DictionaryValue* third_tab_info;
  ASSERT_TRUE(tabs_list_with_permission->GetDictionary(0, &third_tab_info));
  int third_tab_id;
  ASSERT_TRUE(third_tab_info->GetInteger("id", &third_tab_id));
  EXPECT_EQ(ExtensionTabUtil::GetTabId(web_contentses[2]), third_tab_id);
}

}  // namespace extensions
