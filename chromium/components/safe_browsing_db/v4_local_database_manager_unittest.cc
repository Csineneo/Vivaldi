// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/scoped_temp_dir.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/test/test_simple_task_runner.h"
#include "components/safe_browsing_db/v4_database.h"
#include "components/safe_browsing_db/v4_local_database_manager.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/platform_test.h"

namespace safe_browsing {

class FakeV4Database : public V4Database {
 public:
  FakeV4Database(const scoped_refptr<base::SequencedTaskRunner>& db_task_runner,
                 std::unique_ptr<StoreMap> store_map,
                 const StoreAndHashPrefixes& store_and_hash_prefixes)
      : V4Database(db_task_runner, std::move(store_map)),
        store_and_hash_prefixes_(store_and_hash_prefixes) {}

  void GetStoresMatchingFullHash(
      const FullHash& full_hash,
      const StoresToCheck& stores_to_check,
      StoreAndHashPrefixes* store_and_hash_prefixes) override {
    *store_and_hash_prefixes = store_and_hash_prefixes_;
  }

 private:
  const StoreAndHashPrefixes& store_and_hash_prefixes_;
};

class TestClient : public SafeBrowsingDatabaseManager::Client {
 public:
  TestClient(SBThreatType sb_threat_type, const GURL& url)
      : expected_sb_threat_type(sb_threat_type), expected_url(url) {}

  void OnCheckBrowseUrlResult(const GURL& url,
                              SBThreatType threat_type,
                              const ThreatMetadata& metadata) override {
    DCHECK_EQ(expected_url, url);
    DCHECK_EQ(expected_sb_threat_type, threat_type);
  }

  SBThreatType expected_sb_threat_type;
  GURL expected_url;
};

class V4LocalDatabaseManagerTest : public PlatformTest {
 public:
  V4LocalDatabaseManagerTest() : task_runner_(new base::TestSimpleTaskRunner) {}

  void SetUp() override {
    PlatformTest::SetUp();

    ASSERT_TRUE(base_dir_.CreateUniqueTempDir());
    DVLOG(1) << "base_dir_: " << base_dir_.GetPath().value();

    v4_local_database_manager_ =
        make_scoped_refptr(new V4LocalDatabaseManager(base_dir_.GetPath()));
    v4_local_database_manager_->SetTaskRunnerForTest(task_runner_);

    StartLocalDatabaseManager();
  }

  void TearDown() override {
    StopLocalDatabaseManager();

    PlatformTest::TearDown();
  }

  void ForceDisableLocalDatabaseManager() {
    v4_local_database_manager_->enabled_ = false;
  }

  const V4LocalDatabaseManager::QueuedChecks& GetQueuedChecks() {
    return v4_local_database_manager_->queued_checks_;
  }

  void ReplaceV4Database(const StoreAndHashPrefixes& store_and_hash_prefixes) {
    v4_local_database_manager_->v4_database_.reset(new FakeV4Database(
        task_runner_, base::MakeUnique<StoreMap>(), store_and_hash_prefixes));
  }

  void ResetV4Database() { v4_local_database_manager_->v4_database_.reset(); }

  void StartLocalDatabaseManager() {
    v4_local_database_manager_->StartOnIOThread(NULL, V4ProtocolConfig());

    task_runner_->RunPendingTasks();
    base::RunLoop().RunUntilIdle();
  }

  void StopLocalDatabaseManager() {
    if (v4_local_database_manager_) {
      v4_local_database_manager_->StopOnIOThread(true);
    }

    // Force destruction of the database.
    task_runner_->RunPendingTasks();
    base::RunLoop().RunUntilIdle();
  }

  base::ScopedTempDir base_dir_;
  scoped_refptr<base::TestSimpleTaskRunner> task_runner_;
  content::TestBrowserThreadBundle thread_bundle_;
  scoped_refptr<V4LocalDatabaseManager> v4_local_database_manager_;
};

TEST_F(V4LocalDatabaseManagerTest, TestGetThreatSource) {
  EXPECT_EQ(ThreatSource::LOCAL_PVER4,
            v4_local_database_manager_->GetThreatSource());
}

TEST_F(V4LocalDatabaseManagerTest, TestIsSupported) {
  EXPECT_TRUE(v4_local_database_manager_->IsSupported());
}

TEST_F(V4LocalDatabaseManagerTest, TestCanCheckUrl) {
  EXPECT_TRUE(
      v4_local_database_manager_->CanCheckUrl(GURL("http://example.com/a/")));
  EXPECT_TRUE(
      v4_local_database_manager_->CanCheckUrl(GURL("https://example.com/a/")));
  EXPECT_TRUE(
      v4_local_database_manager_->CanCheckUrl(GURL("ftp://example.com/a/")));
  EXPECT_FALSE(
      v4_local_database_manager_->CanCheckUrl(GURL("adp://example.com/a/")));
}

TEST_F(V4LocalDatabaseManagerTest,
       TestCheckBrowseUrlWithEmptyStoresReturnsNoMatch) {
  // Both the stores are empty right now so CheckBrowseUrl should return true.
  EXPECT_TRUE(v4_local_database_manager_->CheckBrowseUrl(
      GURL("http://example.com/a/"), nullptr));
}

TEST_F(V4LocalDatabaseManagerTest, TestCheckBrowseUrlWithFakeDbReturnsMatch) {
  net::TestURLFetcherFactory factory;

  StoreAndHashPrefixes store_and_hash_prefixes;
  store_and_hash_prefixes.emplace_back(GetUrlMalwareId(), HashPrefix("aaaa"));
  ReplaceV4Database(store_and_hash_prefixes);

  // The fake database returns a matched hash prefix.
  EXPECT_FALSE(v4_local_database_manager_->CheckBrowseUrl(
      GURL("http://example.com/a/"), nullptr));
}

TEST_F(V4LocalDatabaseManagerTest,
       TestCheckBrowseUrlReturnsNoMatchWhenDisabled) {
  StoreAndHashPrefixes store_and_hash_prefixes;
  store_and_hash_prefixes.emplace_back(GetUrlMalwareId(), HashPrefix("aaaa"));
  ReplaceV4Database(store_and_hash_prefixes);

  // The same URL returns |false| in the previous test because
  // v4_local_database_manager_ is enabled.
  ForceDisableLocalDatabaseManager();

  EXPECT_TRUE(v4_local_database_manager_->CheckBrowseUrl(
      GURL("http://example.com/a/"), nullptr));
}

TEST_F(V4LocalDatabaseManagerTest, TestGetSeverestThreatTypeAndMetadata) {
  FullHashInfo fhi_malware(FullHash("Malware"), GetUrlMalwareId(),
                           base::Time::Now());
  fhi_malware.metadata.population_id = "malware_popid";

  FullHashInfo fhi_api(FullHash("api"), GetChromeUrlApiId(), base::Time::Now());
  fhi_api.metadata.population_id = "api_popid";

  std::vector<FullHashInfo> fhis({fhi_malware, fhi_api});

  SBThreatType result_threat_type;
  ThreatMetadata metadata;

  v4_local_database_manager_->GetSeverestThreatTypeAndMetadata(
      &result_threat_type, &metadata, fhis);
  EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, result_threat_type);
  EXPECT_EQ("malware_popid", metadata.population_id);

  // Reversing the list has no effect.
  std::reverse(std::begin(fhis), std::end(fhis));
  v4_local_database_manager_->GetSeverestThreatTypeAndMetadata(
      &result_threat_type, &metadata, fhis);
  EXPECT_EQ(SB_THREAT_TYPE_URL_MALWARE, result_threat_type);
  EXPECT_EQ("malware_popid", metadata.population_id);
}

TEST_F(V4LocalDatabaseManagerTest, TestChecksAreQueued) {
  const GURL url("https://www.example.com/");
  TestClient client(SB_THREAT_TYPE_SAFE, url);
  EXPECT_TRUE(GetQueuedChecks().empty());
  v4_local_database_manager_->CheckBrowseUrl(url, &client);
  // The database is available so the check shouldn't get queued.
  EXPECT_TRUE(GetQueuedChecks().empty());

  ResetV4Database();
  v4_local_database_manager_->CheckBrowseUrl(url, &client);
  // The database is unavailable so the check should get queued.
  EXPECT_EQ(1ul, GetQueuedChecks().size());

  // The following function calls StartOnIOThread which should load the
  // database from disk and cause the queued check to be performed.
  StartLocalDatabaseManager();
  EXPECT_TRUE(GetQueuedChecks().empty());

  ResetV4Database();
  v4_local_database_manager_->CheckBrowseUrl(url, &client);
  // The database is unavailable so the check should get queued.
  EXPECT_EQ(1ul, GetQueuedChecks().size());

  StopLocalDatabaseManager();
  EXPECT_TRUE(GetQueuedChecks().empty());
}

}  // namespace safe_browsing
