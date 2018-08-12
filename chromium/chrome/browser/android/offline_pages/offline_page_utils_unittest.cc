// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/offline_pages/offline_page_utils.h"

#include <stdint.h>
#include <utility>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/test/test_simple_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "chrome/browser/android/offline_pages/offline_page_model_factory.h"
#include "chrome/browser/android/offline_pages/test_offline_page_model_builder.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/test/base/testing_profile.h"
#include "components/offline_pages/offline_page_model.h"
#include "components/offline_pages/offline_page_switches.h"
#include "components/offline_pages/offline_page_test_archiver.h"
#include "components/offline_pages/offline_page_test_store.h"
#include "net/base/filename_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace offline_pages {
namespace {

const GURL kTestPage1Url("http://test.org/page1");
const GURL kTestPage2Url("http://test.org/page2");
const GURL kTestPage3Url("http://test.org/page3");
const int64_t kTestPage1BookmarkId = 1234;
const int64_t kTestPage2BookmarkId = 5678;
const int64_t kTestFileSize = 876543LL;

}  // namespace

class OfflinePageUtilsTest
    : public testing::Test,
      public OfflinePageTestArchiver::Observer,
      public base::SupportsWeakPtr<OfflinePageUtilsTest> {
 public:
  OfflinePageUtilsTest();
  ~OfflinePageUtilsTest() override;

  void SetUp() override;
  void RunUntilIdle();

  // Necessary callbacks for the offline page model.
  void OnSavePageDone(OfflinePageModel::SavePageResult result);
  void OnClearAllDone();

  // OfflinePageTestArchiver::Observer implementation:
  void SetLastPathCreatedByArchiver(const base::FilePath& file_path) override;

  // Offline page URL for the first page.
  const GURL& offline_url_page_1() const { return offline_url_page_1_; }
  // Offline page URL for the second page.
  const GURL& offline_url_page_2() const { return offline_url_page_2_; }
  // Offline page URL not related to any page.
  const GURL& offline_url_missing() const { return offline_url_missing_; }

  TestingProfile* profile() { return &profile_; }

 private:
  void CreateOfflinePages();
  scoped_ptr<OfflinePageTestArchiver> BuildArchiver(
      const GURL& url,
      const base::FilePath& file_name);

  GURL offline_url_page_1_;
  GURL offline_url_page_2_;
  GURL offline_url_missing_;

  scoped_refptr<base::TestSimpleTaskRunner> task_runner_;
  base::ThreadTaskRunnerHandle task_runner_handle_;
  TestingProfile profile_;
};

OfflinePageUtilsTest::OfflinePageUtilsTest()
    : task_runner_(new base::TestSimpleTaskRunner),
      task_runner_handle_(task_runner_) {}

OfflinePageUtilsTest::~OfflinePageUtilsTest() {}

void OfflinePageUtilsTest::SetUp() {
  // Enable offline pages feature.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableOfflinePages);

  // Set up the factory for testing.
  OfflinePageModelFactory::GetInstance()->SetTestingFactoryAndUse(
      &profile_, BuildTestOfflinePageModel);
  RunUntilIdle();

  // Make sure the store contains the right offline pages before the load
  // happens.
  CreateOfflinePages();
}

void OfflinePageUtilsTest::RunUntilIdle() {
  task_runner_->RunUntilIdle();
}

void OfflinePageUtilsTest::OnSavePageDone(
    OfflinePageModel::SavePageResult result) {
  // Result ignored here.
}

void OfflinePageUtilsTest::OnClearAllDone() {
  // Result ignored here.
}

void OfflinePageUtilsTest::SetLastPathCreatedByArchiver(
    const base::FilePath& file_path) {}

void OfflinePageUtilsTest::CreateOfflinePages() {
  OfflinePageModel* model =
      OfflinePageModelFactory::GetForBrowserContext(profile());

  // Create page 1.
  scoped_ptr<OfflinePageTestArchiver> archiver(BuildArchiver(
      kTestPage1Url, base::FilePath(FILE_PATH_LITERAL("page1.mhtml"))));
  model->SavePage(
      kTestPage1Url, kTestPage1BookmarkId, std::move(archiver),
      base::Bind(&OfflinePageUtilsTest::OnSavePageDone, AsWeakPtr()));
  RunUntilIdle();

  // Create page 2.
  archiver = BuildArchiver(kTestPage2Url,
                           base::FilePath(FILE_PATH_LITERAL("page2.mhtml")));
  model->SavePage(
      kTestPage2Url, kTestPage2BookmarkId, std::move(archiver),
      base::Bind(&OfflinePageUtilsTest::OnSavePageDone, AsWeakPtr()));
  RunUntilIdle();

  // Make a copy of local paths of the two pages stored in the model.
  offline_url_page_1_ =
      model->GetPageByBookmarkId(kTestPage1BookmarkId)->GetOfflineURL();
  offline_url_page_2_ =
      model->GetPageByBookmarkId(kTestPage2BookmarkId)->GetOfflineURL();
  // Create a file path that is not associated with any offline page.
  offline_url_missing_ = net::FilePathToFileURL(
      profile()
          ->GetPath()
          .Append(chrome::kOfflinePageArchviesDirname)
          .Append(FILE_PATH_LITERAL("missing_file.mhtml")));
}

scoped_ptr<OfflinePageTestArchiver> OfflinePageUtilsTest::BuildArchiver(
    const GURL& url,
    const base::FilePath& file_name) {
  scoped_ptr<OfflinePageTestArchiver> archiver(new OfflinePageTestArchiver(
      this, url, OfflinePageArchiver::ArchiverResult::SUCCESSFULLY_CREATED,
      kTestFileSize, base::ThreadTaskRunnerHandle::Get()));
  archiver->set_filename(file_name);
  return archiver;
}

// Simple test for offline page model having any pages loaded.
TEST_F(OfflinePageUtilsTest, HasOfflinePages) {
  EXPECT_TRUE(OfflinePageUtils::HasOfflinePages(profile()));

  OfflinePageModelFactory::GetForBrowserContext(profile())->ClearAll(
      base::Bind(&OfflinePageUtilsTest::OnClearAllDone, AsWeakPtr()));
  RunUntilIdle();

  EXPECT_FALSE(OfflinePageUtils::HasOfflinePages(profile()));
}

TEST_F(OfflinePageUtilsTest, MightBeOfflineURL) {
  // URL is invalid.
  EXPECT_FALSE(OfflinePageUtils::MightBeOfflineURL(GURL("/test.mhtml")));
  // Scheme is not file.
  EXPECT_FALSE(OfflinePageUtils::MightBeOfflineURL(GURL("http://test.com/")));
  // Does not end with .mhtml.
  EXPECT_FALSE(OfflinePageUtils::MightBeOfflineURL(GURL("file:///test.txt")));
  // Might still be an offline page.
  EXPECT_TRUE(OfflinePageUtils::MightBeOfflineURL(GURL("file:///test.mhtml")));
}

TEST_F(OfflinePageUtilsTest, GetOfflineURLForOnlineURL) {
  EXPECT_EQ(offline_url_page_1(), OfflinePageUtils::GetOfflineURLForOnlineURL(
                                      profile(), kTestPage1Url));
  EXPECT_EQ(offline_url_page_2(), OfflinePageUtils::GetOfflineURLForOnlineURL(
                                      profile(), kTestPage2Url));
  EXPECT_EQ(GURL(), OfflinePageUtils::GetOfflineURLForOnlineURL(
                        profile(), GURL(kTestPage3Url)));
}

TEST_F(OfflinePageUtilsTest, GetOnlineURLForOfflineURL) {
  EXPECT_EQ(kTestPage1Url, OfflinePageUtils::GetOnlineURLForOfflineURL(
                               profile(), offline_url_page_1()));
  EXPECT_EQ(kTestPage2Url, OfflinePageUtils::GetOnlineURLForOfflineURL(
                               profile(), offline_url_page_2()));
  EXPECT_EQ(GURL::EmptyGURL(), OfflinePageUtils::GetOfflineURLForOnlineURL(
                                   profile(), offline_url_missing()));
}

TEST_F(OfflinePageUtilsTest, GetBookmarkIdForOfflineURL) {
  EXPECT_EQ(kTestPage1BookmarkId, OfflinePageUtils::GetBookmarkIdForOfflineURL(
                                      profile(), offline_url_page_1()));
  EXPECT_EQ(kTestPage2BookmarkId, OfflinePageUtils::GetBookmarkIdForOfflineURL(
                                      profile(), offline_url_page_2()));
  EXPECT_EQ(-1, OfflinePageUtils::GetBookmarkIdForOfflineURL(
                    profile(), offline_url_missing()));
}

TEST_F(OfflinePageUtilsTest, IsOfflinePage) {
  EXPECT_TRUE(OfflinePageUtils::IsOfflinePage(profile(), offline_url_page_1()));
  EXPECT_TRUE(OfflinePageUtils::IsOfflinePage(profile(), offline_url_page_2()));
  EXPECT_FALSE(
      OfflinePageUtils::IsOfflinePage(profile(), offline_url_missing()));
  EXPECT_FALSE(OfflinePageUtils::IsOfflinePage(profile(), kTestPage1Url));
  EXPECT_FALSE(OfflinePageUtils::IsOfflinePage(profile(), kTestPage2Url));
}

TEST_F(OfflinePageUtilsTest, HasOfflinePageForOnlineURL) {
  EXPECT_TRUE(
      OfflinePageUtils::HasOfflinePageForOnlineURL(profile(), kTestPage1Url));
  EXPECT_TRUE(
      OfflinePageUtils::HasOfflinePageForOnlineURL(profile(), kTestPage2Url));
  EXPECT_FALSE(
      OfflinePageUtils::HasOfflinePageForOnlineURL(profile(), kTestPage3Url));
}

}  // namespace offline_pages
