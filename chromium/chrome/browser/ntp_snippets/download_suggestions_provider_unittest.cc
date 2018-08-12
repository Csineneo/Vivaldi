// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ntp_snippets/download_suggestions_provider.h"

#include <memory>

#include "base/bind.h"
#include "base/observer_list.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ntp_snippets/fake_download_item.h"
#include "components/ntp_snippets/category.h"
#include "components/ntp_snippets/category_factory.h"
#include "components/ntp_snippets/mock_content_suggestions_provider_observer.h"
#include "components/ntp_snippets/offline_pages/offline_pages_test_utils.h"
#include "components/offline_pages/client_namespace_constants.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/mock_download_item.h"
#include "content/public/test/mock_download_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::DownloadItem;
using content::MockDownloadManager;
using ntp_snippets::Category;
using ntp_snippets::CategoryFactory;
using ntp_snippets::ContentSuggestion;
using ntp_snippets::ContentSuggestionsProvider;
using ntp_snippets::MockContentSuggestionsProviderObserver;
using ntp_snippets::test::CaptureDismissedSuggestions;
using ntp_snippets::test::FakeOfflinePageModel;
using ntp_snippets::CategoryStatus;
using offline_pages::ClientId;
using offline_pages::OfflinePageItem;
using test::FakeDownloadItem;
using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Lt;
using testing::Mock;
using testing::Return;
using testing::SizeIs;
using testing::StrictMock;
using testing::UnorderedElementsAre;

namespace ntp_snippets {
// These functions are implicitly used to print out values during the tests.
std::ostream& operator<<(std::ostream& os, const ContentSuggestion& value) {
  os << "{ url: " << value.url() << ", publish_date: " << value.publish_date()
     << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const CategoryStatus& value) {
  os << "CategoryStatus::";
  switch (value) {
    case CategoryStatus::INITIALIZING:
      os << "INITIALIZING";
      break;
    case CategoryStatus::AVAILABLE:
      os << "AVAILABLE";
      break;
    case CategoryStatus::AVAILABLE_LOADING:
      os << "AVAILABLE_LOADING";
      break;
    case CategoryStatus::NOT_PROVIDED:
      os << "NOT_PROVIDED";
      break;
    case CategoryStatus::ALL_SUGGESTIONS_EXPLICITLY_DISABLED:
      os << "ALL_SUGGESTIONS_EXPLICITLY_DISABLED";
      break;
    case CategoryStatus::CATEGORY_EXPLICITLY_DISABLED:
      os << "CATEGORY_EXPLICITLY_DISABLED";
      break;
    case CategoryStatus::SIGNED_OUT:
      os << "SIGNED_OUT";
      break;
    case CategoryStatus::LOADING_ERROR:
      os << "LOADING_ERROR";
      break;
  }
  return os;
}

}  // namespace ntp_snippets

namespace {

// TODO(vitaliii): Move this and outputting functions above to common file and
// replace remaining |Property(&ContentSuggestion::url, GURL("some_url"))|.
// See crbug.com/655513.
MATCHER_P(HasUrl, url, "") {
  *result_listener << "expected URL: " << url
                   << "has URL: " << arg.url().spec();
  return arg.url().spec() == url;
}

MATCHER_P3(HasDownloadSuggestionExtra,
           is_download_asset,
           target_file_path,
           mime_type,
           "") {
  if (arg.download_suggestion_extra() == nullptr) {
    *result_listener << "has no download_suggestion_extra";
    return false;
  }
  auto extra = *arg.download_suggestion_extra();
  *result_listener << "expected download asset?: " << is_download_asset
                   << "\n actual is download asset?: "
                   << extra.is_download_asset;
  if (extra.is_download_asset != is_download_asset) {
    return false;
  }
  *result_listener << "expected target_file_path: "
                   << target_file_path
                   << "\nactual target_file_path: "
                   << extra.target_file_path.value();
  if (extra.target_file_path.value() !=
      base::FilePath::StringType(target_file_path)) {
    return false;
  }
  *result_listener << "expected mime_type: " << mime_type
                   << "\nactual mime_type: "
                   << extra.mime_type;
  return extra.mime_type == mime_type;
}

OfflinePageItem CreateDummyOfflinePage(int id) {
  return ntp_snippets::test::CreateDummyOfflinePageItem(
      id, offline_pages::kAsyncNamespace);
}

std::vector<OfflinePageItem> CreateDummyOfflinePages(
    const std::vector<int>& ids) {
  std::vector<OfflinePageItem> result;
  for (int id : ids)
    result.push_back(CreateDummyOfflinePage(id));

  return result;
}

OfflinePageItem CreateDummyOfflinePage(int id, base::Time time) {
  OfflinePageItem item = CreateDummyOfflinePage(id);
  item.creation_time = time;
  return item;
}

std::unique_ptr<FakeDownloadItem> CreateDummyAssetDownload(int id) {
  std::unique_ptr<FakeDownloadItem> item = base::MakeUnique<FakeDownloadItem>();
  item->SetId(id);
  std::string id_string = base::IntToString(id);
  item->SetTargetFilePath(
      base::FilePath::FromUTF8Unsafe("folder/file" + id_string + ".mhtml"));
  item->SetURL(GURL("http://download.com/redirected" + id_string));
  item->SetOriginalUrl(GURL("http://download.com/" + id_string));
  item->SetStartTime(base::Time::Now());
  item->SetFileExternallyRemoved(false);
  item->SetState(DownloadItem::DownloadState::COMPLETE);
  item->SetMimeType("application/pdf");
  return item;
}

std::unique_ptr<FakeDownloadItem> CreateDummyAssetDownload(
    int id,
    const base::Time& start_time) {
  std::unique_ptr<FakeDownloadItem> item = CreateDummyAssetDownload(id);
  item->SetStartTime(start_time);
  return item;
}

std::vector<std::unique_ptr<FakeDownloadItem>> CreateDummyAssetDownloads(
    const std::vector<int>& ids) {
  std::vector<std::unique_ptr<FakeDownloadItem>> result;
  // The time is set to enforce the provider to cache the first items in the
  // list first.
  base::Time current_time = base::Time::Now();
  for (int id : ids) {
    result.push_back(CreateDummyAssetDownload(id, current_time));
    current_time -= base::TimeDelta::FromDays(1);
  }
  return result;
}

class ObservedMockDownloadManager : public MockDownloadManager {
 public:
  ObservedMockDownloadManager() {}
  ~ObservedMockDownloadManager() override {
    for (auto& observer : observers_)
      observer.ManagerGoingDown(this);
  }

  // Observer accessors.
  void AddObserver(Observer* observer) override {
    observers_.AddObserver(observer);
  }

  void RemoveObserver(Observer* observer) override {
    observers_.RemoveObserver(observer);
  }

  void NotifyDownloadCreated(DownloadItem* item) {
    for (auto& observer : observers_)
      observer.OnDownloadCreated(this, item);
  }

  std::vector<std::unique_ptr<FakeDownloadItem>>* mutable_items() {
    return &items_;
  }

  const std::vector<std::unique_ptr<FakeDownloadItem>>& items() const {
    return items_;
  }

  void GetAllDownloads(std::vector<DownloadItem*>* all_downloads) override {
    all_downloads->clear();
    for (const auto& item : items_)
      all_downloads->push_back(item.get());
  }

 private:
  base::ObserverList<Observer> observers_;
  std::vector<std::unique_ptr<FakeDownloadItem>> items_;
};

}  // namespace

class DownloadSuggestionsProviderTest : public testing::Test {
 public:
  DownloadSuggestionsProviderTest()
      : pref_service_(new TestingPrefServiceSimple()) {
    DownloadSuggestionsProvider::RegisterProfilePrefs(
        pref_service()->registry());
  }

  void IgnoreOnCategoryStatusChangedToAvailable() {
    EXPECT_CALL(observer_, OnCategoryStatusChanged(_, downloads_category(),
                                                   CategoryStatus::AVAILABLE))
        .Times(AnyNumber());
    EXPECT_CALL(observer_,
                OnCategoryStatusChanged(_, downloads_category(),
                                        CategoryStatus::AVAILABLE_LOADING))
        .Times(AnyNumber());
  }

  void IgnoreOnSuggestionInvalidated() {
    EXPECT_CALL(observer_, OnSuggestionInvalidated(_, _)).Times(AnyNumber());
  }

  DownloadSuggestionsProvider* CreateProvider(bool show_assets,
                                              bool show_offline_pages) {
    DCHECK(!provider_);
    DCHECK(show_assets || show_offline_pages);
    provider_ = base::MakeUnique<DownloadSuggestionsProvider>(
        &observer_, &category_factory_,
        show_offline_pages ? &offline_pages_model_ : nullptr,
        show_assets ? &downloads_manager_ : nullptr, pref_service(),
        /*download_manager_ui_enabled=*/false);
    return provider_.get();
  }

  void DestroyProvider() { provider_.reset(); }

  Category downloads_category() {
    return category_factory_.FromKnownCategory(
        ntp_snippets::KnownCategories::DOWNLOADS);
  }

  void FireOfflinePageModelLoaded() {
    DCHECK(provider_);
    provider_->OfflinePageModelLoaded(&offline_pages_model_);
  }

  void FireOfflinePageModelChanged() {
    DCHECK(provider_);
    provider_->OfflinePageModelChanged(&offline_pages_model_);
  }

  void FireOfflinePageDeleted(const OfflinePageItem& item) {
    DCHECK(provider_);
    provider_->OfflinePageDeleted(item.offline_id, item.client_id);
  }

  void FireDownloadCreated(DownloadItem* item) {
    DCHECK(provider_);
    downloads_manager_.NotifyDownloadCreated(item);
  }

  void FireDownloadsCreated(
      const std::vector<std::unique_ptr<FakeDownloadItem>>& items) {
    for (const auto& item : items)
      FireDownloadCreated(item.get());
  }

  ContentSuggestion::ID GetDummySuggestionId(int id, bool is_offline_page) {
    return ContentSuggestion::ID(
        downloads_category(),
        (is_offline_page ? "O" : "D") + base::IntToString(id));
  }

  std::vector<ContentSuggestion> GetDismissedSuggestions() {
    std::vector<ContentSuggestion> dismissed_suggestions;
    // This works synchronously because both fake data sources were designed so.
    provider()->GetDismissedSuggestionsForDebugging(
        downloads_category(),
        base::Bind(&CaptureDismissedSuggestions, &dismissed_suggestions));
    return dismissed_suggestions;
  }

  ContentSuggestionsProvider* provider() {
    DCHECK(provider_);
    return provider_.get();
  }
  ObservedMockDownloadManager* downloads_manager() {
    return &downloads_manager_;
  }
  FakeOfflinePageModel* offline_pages_model() { return &offline_pages_model_; }
  MockContentSuggestionsProviderObserver* observer() { return &observer_; }
  TestingPrefServiceSimple* pref_service() { return pref_service_.get(); }

 private:
  ObservedMockDownloadManager downloads_manager_;
  FakeOfflinePageModel offline_pages_model_;
  StrictMock<MockContentSuggestionsProviderObserver> observer_;
  CategoryFactory category_factory_;
  std::unique_ptr<TestingPrefServiceSimple> pref_service_;
  // Last so that the dependencies are deleted after the provider.
  std::unique_ptr<DownloadSuggestionsProvider> provider_;

  DISALLOW_COPY_AND_ASSIGN(DownloadSuggestionsProviderTest);
};

TEST_F(DownloadSuggestionsProviderTest,
       ShouldConvertOfflinePagesToSuggestions) {
  IgnoreOnCategoryStatusChangedToAvailable();

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  EXPECT_CALL(*observer(),
              OnNewSuggestions(
                  _, downloads_category(),
                  UnorderedElementsAre(AllOf(HasUrl("http://dummy.com/1"),
                                             HasDownloadSuggestionExtra(
                                                 /*is_download_asset=*/false,
                                                 FILE_PATH_LITERAL(""), "")),
                                       AllOf(HasUrl("http://dummy.com/2"),
                                             HasDownloadSuggestionExtra(
                                                 /*is_download_asset=*/false,
                                                 FILE_PATH_LITERAL(""), "")))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldConvertDownloadItemsToSuggestions) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(0)));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);

  std::vector<std::unique_ptr<FakeDownloadItem>> asset_downloads =
      CreateDummyAssetDownloads({1, 2});

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(),
                               UnorderedElementsAre(AllOf(
                                   HasUrl("http://download.com/1"),
                                   HasDownloadSuggestionExtra(
                                       /*is_download_asset=*/true,
                                       FILE_PATH_LITERAL("folder/file1.mhtml"),
                                       "application/pdf")))));
  FireDownloadCreated(asset_downloads[0].get());

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(
                           AllOf(HasUrl("http://download.com/1"),
                                 HasDownloadSuggestionExtra(
                                     /*is_download_asset=*/true,
                                     FILE_PATH_LITERAL("folder/file1.mhtml"),
                                     "application/pdf")),
                           AllOf(HasUrl("http://download.com/2"),
                                 HasDownloadSuggestionExtra(
                                     /*is_download_asset=*/true,
                                     FILE_PATH_LITERAL("folder/file2.mhtml"),
                                     "application/pdf")))));
  FireDownloadCreated(asset_downloads[1].get());
}

TEST_F(DownloadSuggestionsProviderTest, ShouldMixInBothSources) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(),
                                            UnorderedElementsAre(
                                                HasUrl("http://dummy.com/1"),
                                                HasUrl("http://dummy.com/2"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);

  std::vector<std::unique_ptr<FakeDownloadItem>> asset_downloads =
      CreateDummyAssetDownloads({1, 2});

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"))));
  FireDownloadCreated(asset_downloads[0].get());

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  FireDownloadCreated(asset_downloads[1].get());
}

TEST_F(DownloadSuggestionsProviderTest, ShouldSortSuggestions) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  base::Time now = base::Time::Now();
  base::Time yesterday = now - base::TimeDelta::FromDays(1);
  base::Time tomorrow = now + base::TimeDelta::FromDays(1);
  base::Time next_week = now + base::TimeDelta::FromDays(7);

  (*offline_pages_model()->mutable_items())
      .push_back(CreateDummyOfflinePage(1, yesterday));
  (*offline_pages_model()->mutable_items())
      .push_back(CreateDummyOfflinePage(2, tomorrow));

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(),
                               ElementsAre(HasUrl("http://dummy.com/2"),
                                           HasUrl("http://dummy.com/1"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);

  std::vector<std::unique_ptr<FakeDownloadItem>> asset_downloads;
  asset_downloads.push_back(CreateDummyAssetDownload(3, next_week));
  asset_downloads.push_back(CreateDummyAssetDownload(4, now));

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(),
                               ElementsAre(HasUrl("http://download.com/3"),
                                           HasUrl("http://dummy.com/2"),
                                           HasUrl("http://dummy.com/1"))));
  FireDownloadCreated(asset_downloads[0].get());

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(),
                               ElementsAre(HasUrl("http://download.com/3"),
                                           HasUrl("http://dummy.com/2"),
                                           HasUrl("http://download.com/4"),
                                           HasUrl("http://dummy.com/1"))));
  FireDownloadCreated(asset_downloads[1].get());
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldDismissWithoutNotifyingObservers) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(4ul))))
      .Times(2);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  FireDownloadsCreated(downloads_manager()->items());

  EXPECT_CALL(*observer(), OnNewSuggestions(_, _, _)).Times(0);
  EXPECT_CALL(*observer(), OnSuggestionInvalidated(_, _)).Times(0);
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));

  // |downloads_manager_| is destroyed after the |provider_|, so the provider
  // will not observe download items being destroyed.
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldNotReportDismissedSuggestionsOnNewData) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(4ul))))
      .Times(2);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  FireDownloadsCreated(downloads_manager()->items());

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/2"))));
  FireOfflinePageModelChanged();
}

TEST_F(DownloadSuggestionsProviderTest, ShouldReturnDismissedSuggestions) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(4ul))))
      .Times(2);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  FireDownloadsCreated(downloads_manager()->items());

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));

  EXPECT_THAT(GetDismissedSuggestions(),
              UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                   HasUrl("http://download.com/1")));
}

TEST_F(DownloadSuggestionsProviderTest, ShouldClearDismissedSuggestions) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(4ul))))
      .Times(2);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  FireDownloadsCreated(downloads_manager()->items());

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  provider()->ClearDismissedSuggestionsForDebugging(downloads_category());
  EXPECT_THAT(GetDismissedSuggestions(), IsEmpty());
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldNotDismissOtherTypeWithTheSameID) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(4ul))))
      .Times(2);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  FireDownloadsCreated(downloads_manager()->items());

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  FireOfflinePageModelChanged();
}

TEST_F(DownloadSuggestionsProviderTest, ShouldReplaceDismissedItemWithNewData) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(5ul))))
      .Times(5);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"),
                                            HasUrl("http://download.com/3"),
                                            HasUrl("http://download.com/4"),
                                            HasUrl("http://download.com/5"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  // Currently the provider stores five items in its internal cache, so six
  // items are needed to check whether all downloads are fetched on dismissal.
  *(downloads_manager()->mutable_items()) =
      CreateDummyAssetDownloads({1, 2, 3, 4, 5, 6});
  FireDownloadsCreated(downloads_manager()->items());

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));

  // The provider is not notified about the 6th item, however, it must report
  // it now.
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/2"),
                                            HasUrl("http://download.com/3"),
                                            HasUrl("http://download.com/4"),
                                            HasUrl("http://download.com/5"),
                                            HasUrl("http://download.com/6"))));
  FireOfflinePageModelChanged();
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldInvalidateWhenUnderlyingItemDeleted) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(3ul))));
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"))));
  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1});
  FireDownloadsCreated(downloads_manager()->items());

  // We add another item manually, so that when it gets deleted it is not
  // present in DownloadsManager list.
  std::unique_ptr<FakeDownloadItem> removed_item = CreateDummyAssetDownload(2);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                            HasUrl("http://dummy.com/2"),
                                            HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  FireDownloadCreated(removed_item.get());

  EXPECT_CALL(*observer(),
              OnSuggestionInvalidated(
                  _, GetDummySuggestionId(1, /*is_offline_page=*/true)));
  FireOfflinePageDeleted(offline_pages_model()->items()[0]);

  EXPECT_CALL(*observer(),
              OnSuggestionInvalidated(
                  _, GetDummySuggestionId(2, /*is_offline_page=*/false)));
  // |OnDownloadItemDestroyed| is called from |removed_item|'s destructor.
  removed_item.reset();
}

TEST_F(DownloadSuggestionsProviderTest, ShouldReplaceRemovedItemWithNewData) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(5ul))))
      .Times(5);
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"),
                                            HasUrl("http://download.com/3"),
                                            HasUrl("http://download.com/4"),
                                            HasUrl("http://download.com/5"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) =
      CreateDummyAssetDownloads({1, 2, 3, 4, 5});
  FireDownloadsCreated(downloads_manager()->items());

  // Note that |CreateDummyAssetDownloads| creates items "downloaded" before
  // |base::Time::Now()|, so for a new item the time is set in future to enforce
  // the provider to show the new item.
  std::unique_ptr<FakeDownloadItem> removed_item = CreateDummyAssetDownload(
      100, base::Time::Now() + base::TimeDelta::FromDays(1));
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(
          _, downloads_category(),
          UnorderedElementsAre(
              HasUrl("http://download.com/1"), HasUrl("http://download.com/2"),
              HasUrl("http://download.com/3"), HasUrl("http://download.com/4"),
              HasUrl("http://download.com/100"))));
  FireDownloadCreated(removed_item.get());

  // |OnDownloadDestroyed| notification is called in |DownloadItem|'s
  // destructor.
  removed_item.reset();

  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"),
                                            HasUrl("http://download.com/3"),
                                            HasUrl("http://download.com/4"),
                                            HasUrl("http://download.com/5"))));
  FireOfflinePageModelChanged();
}

TEST_F(DownloadSuggestionsProviderTest, ShouldPruneOfflinePagesDismissedIDs) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  *(offline_pages_model()->mutable_items()) =
      CreateDummyOfflinePages({1, 2, 3});
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(),
                                            UnorderedElementsAre(
                                                HasUrl("http://dummy.com/1"),
                                                HasUrl("http://dummy.com/2"),
                                                HasUrl("http://dummy.com/3"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(2, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(3, /*is_offline_page=*/true));
  EXPECT_THAT(GetDismissedSuggestions(), SizeIs(3));

  // Prune on getting all offline pages. Note that the first suggestion is not
  // removed from |offline_pages_model| storage, because otherwise
  // |GetDismissedSuggestions| cannot return it.
  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), IsEmpty()));

  // TODO(vitaliii): Add the first suggestion back, so that
  // GetDismissedSuggestions can return it.
  *(offline_pages_model()->mutable_items()) =
      CreateDummyOfflinePages({2, 3});
  FireOfflinePageModelChanged();
  EXPECT_THAT(GetDismissedSuggestions(), SizeIs(2));

  // Prune when offline page is deleted.
  FireOfflinePageDeleted(offline_pages_model()->items()[1]);
  EXPECT_THAT(GetDismissedSuggestions(), SizeIs(1));
}

TEST_F(DownloadSuggestionsProviderTest, ShouldPruneAssetDownloadsDismissedIDs) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(Lt(3ul))))
      .Times(3);
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  FireDownloadsCreated(downloads_manager()->items());

  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));
  provider()->DismissSuggestion(
      GetDummySuggestionId(2, /*is_offline_page=*/false));
  EXPECT_THAT(GetDismissedSuggestions(), SizeIs(2));

  downloads_manager()->items()[0]->NotifyDownloadDestroyed();
  EXPECT_THAT(GetDismissedSuggestions(), SizeIs(1));
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldFetchAssetDownloadsOnStartupButOnlyOnce) {
  IgnoreOnCategoryStatusChangedToAvailable();

  // Downloads manager was created before the provider, so |OnDownloadCreated|
  // calls "were" missed, but the provider must show missed items anyway.
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldInvalidateAssetDownloadWhenItsFileRemoved) {
  IgnoreOnCategoryStatusChangedToAvailable();

  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), IsEmpty()));
  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), SizeIs(1)));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1});
  FireDownloadsCreated(downloads_manager()->items());

  EXPECT_CALL(*observer(),
              OnSuggestionInvalidated(
                  _, GetDummySuggestionId(1, /*is_offline_page=*/false)));
  (*downloads_manager()->mutable_items())[0]->SetFileExternallyRemoved(true);
  (*downloads_manager()->mutable_items())[0]->NotifyDownloadUpdated();
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldNotShowOfflinePagesWhenTurnedOff) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), IsEmpty()));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/false);

  std::vector<std::unique_ptr<FakeDownloadItem>> asset_downloads =
      CreateDummyAssetDownloads({1});
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/1"))));
  FireDownloadCreated(asset_downloads[0].get());
  // TODO(vitaliii): Notify the provider that an offline page has been updated.
}

TEST_F(DownloadSuggestionsProviderTest, ShouldNotShowAssetsWhenTurnedOff) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(),
                                            UnorderedElementsAre(
                                                HasUrl("http://dummy.com/1"),
                                                HasUrl("http://dummy.com/2"))));
  CreateProvider(/*show_assets=*/false, /*show_offline_pages=*/true);
  downloads_manager()->NotifyDownloadCreated(
      downloads_manager()->items()[0].get());
  // This notification should not reach the provider, because the asset
  // downloads data source is not provided. If it is and the provider reacts to
  // the notification, the test will fail because the observer is a strict mock.
  (*downloads_manager()->mutable_items())[0]->NotifyDownloadUpdated();
}

TEST_F(DownloadSuggestionsProviderTest, ShouldLoadOfflinePagesOnModelLoaded) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  offline_pages_model()->set_is_loaded(false);
  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(), IsEmpty()));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  offline_pages_model()->set_is_loaded(true);
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(),
                                            UnorderedElementsAre(
                                                HasUrl("http://dummy.com/1"),
                                                HasUrl("http://dummy.com/2"))));
  FireOfflinePageModelLoaded();
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldLoadOfflinePagesIfMissesOnModelLoaded) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1, 2});
  offline_pages_model()->set_is_loaded(true);
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(),
                                            UnorderedElementsAre(
                                                HasUrl("http://dummy.com/1"),
                                                HasUrl("http://dummy.com/2"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldLoadAndSubmitMissedAssetsEvenIfOfflinePagesAreTurnedOff) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1, 2});
  EXPECT_CALL(
      *observer(),
      OnNewSuggestions(_, downloads_category(),
                       UnorderedElementsAre(HasUrl("http://download.com/1"),
                                            HasUrl("http://download.com/2"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/false);
}

TEST_F(DownloadSuggestionsProviderTest,
       ShouldNotPruneDismissedSuggestionsOnStartup) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  // We dismiss an item to store it in the list of dismissed items.
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1});
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(), _));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/false);
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));
  DestroyProvider();

  // We simulate current DownloadManager behaviour;
  // The download manager has not started reading the list yet, so it is empty.
  downloads_manager()->mutable_items()->clear();
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(), _));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/false);
  Mock::VerifyAndClearExpectations(observer());

  // The first download is being read.
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1});
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(), _))
      .Times(0);
  FireDownloadCreated(downloads_manager()->items()[0].get());
  // The first download should not be reported, because it is dismissed.
}

TEST_F(DownloadSuggestionsProviderTest, ShouldStoreDismissedSuggestions) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  // Dismiss items to store them in the list of dismissed items.
  *(offline_pages_model()->mutable_items()) = CreateDummyOfflinePages({1});
  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads({1});
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(), _));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/true));
  provider()->DismissSuggestion(
      GetDummySuggestionId(1, /*is_offline_page=*/false));
  // Destroy and create provider to simulate turning off Chrome.
  DestroyProvider();

  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(), _));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/true);
  EXPECT_THAT(GetDismissedSuggestions(),
              UnorderedElementsAre(HasUrl("http://dummy.com/1"),
                                   HasUrl("http://download.com/1")));
}

// TODO(vitaliii): Remove this test once the dismissed ids are pruned. See
// crbug.com/672758.
TEST_F(DownloadSuggestionsProviderTest, ShouldRemoveOldDismissedIdsIfTooMany) {
  IgnoreOnCategoryStatusChangedToAvailable();
  IgnoreOnSuggestionInvalidated();

  const int kMaxDismissedIdCount =
      DownloadSuggestionsProvider::GetMaxDismissedCountForTesting();
  std::vector<int> ids;
  for (int i = 0; i < kMaxDismissedIdCount + 1; ++i) {
    ids.push_back(i);
  }

  *(downloads_manager()->mutable_items()) = CreateDummyAssetDownloads(ids);
  EXPECT_CALL(*observer(), OnNewSuggestions(_, downloads_category(), _));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/false);

  for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
    provider()->DismissSuggestion(
        GetDummySuggestionId(i, /*is_offline_page=*/false));
  }

  EXPECT_THAT(GetDismissedSuggestions(), SizeIs(kMaxDismissedIdCount));
  DestroyProvider();
  // The oldest dismissed suggestion must become undismissed now. This is a
  // temporary workaround and not what we want in long term. This test must be
  // removed once we start pruning dismissed asset downloads on startup.
  EXPECT_CALL(*observer(),
              OnNewSuggestions(_, downloads_category(),
                               ElementsAre(HasUrl("http://download.com/0"))));
  CreateProvider(/*show_assets=*/true, /*show_offline_pages=*/false);
}
