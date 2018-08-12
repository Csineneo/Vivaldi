// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_driver/device_info_service.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "components/sync_driver/local_device_info_provider_mock.h"
#include "sync/api/data_batch.h"
#include "sync/api/metadata_batch.h"
#include "sync/api/model_type_store.h"
#include "sync/internal_api/public/test/model_type_store_test_util.h"
#include "sync/protocol/data_type_state.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace sync_driver_v2 {

using syncer_v2::DataBatch;
using syncer_v2::EntityData;
using syncer_v2::MetadataBatch;
using syncer_v2::MetadataChangeList;
using syncer_v2::ModelTypeChangeProcessor;
using syncer_v2::ModelTypeService;
using syncer_v2::ModelTypeStore;
using syncer_v2::ModelTypeStoreTestUtil;
using syncer_v2::TagAndData;
using sync_driver::DeviceInfo;
using sync_driver::DeviceInfoTracker;
using sync_driver::LocalDeviceInfoProviderMock;
using sync_pb::DataTypeState;
using sync_pb::DeviceInfoSpecifics;
using sync_pb::EntitySpecifics;

using ClientTagList = ModelTypeService::ClientTagList;
using Result = ModelTypeStore::Result;
using WriteBatch = ModelTypeStore::WriteBatch;

namespace {

void AssertResultIsSuccess(Result result) {
  ASSERT_EQ(Result::SUCCESS, result);
}

void AssertEqual(const DeviceInfoSpecifics& s1, const DeviceInfoSpecifics& s2) {
  ASSERT_EQ(s1.cache_guid(), s2.cache_guid());
  ASSERT_EQ(s1.client_name(), s2.client_name());
  ASSERT_EQ(s1.device_type(), s2.device_type());
  ASSERT_EQ(s1.sync_user_agent(), s2.sync_user_agent());
  ASSERT_EQ(s1.chrome_version(), s2.chrome_version());
  ASSERT_EQ(s1.signin_scoped_device_id(), s2.signin_scoped_device_id());
}

void AssertEqual(const DeviceInfoSpecifics& specifics,
                 const DeviceInfo& model) {
  ASSERT_EQ(specifics.cache_guid(), model.guid());
  ASSERT_EQ(specifics.client_name(), model.client_name());
  ASSERT_EQ(specifics.device_type(), model.device_type());
  ASSERT_EQ(specifics.sync_user_agent(), model.sync_user_agent());
  ASSERT_EQ(specifics.chrome_version(), model.chrome_version());
  ASSERT_EQ(specifics.signin_scoped_device_id(),
            model.signin_scoped_device_id());
}

void AssertErrorFromDataBatch(syncer::SyncError error,
                              scoped_ptr<DataBatch> batch) {
  ASSERT_TRUE(error.IsSet());
}

void AssertExpectedFromDataBatch(
    std::map<std::string, DeviceInfoSpecifics> expected,
    syncer::SyncError error,
    scoped_ptr<DataBatch> batch) {
  ASSERT_FALSE(error.IsSet());
  while (batch->HasNext()) {
    const TagAndData& pair = batch->Next();
    std::map<std::string, DeviceInfoSpecifics>::iterator iter =
        expected.find(pair.first);
    ASSERT_NE(iter, expected.end());
    AssertEqual(iter->second, pair.second->specifics.device_info());
    // Removing allows us to verify we don't see the same item multiple times,
    // and that we saw everything we expected.
    expected.erase(iter);
  }
  ASSERT_TRUE(expected.empty());
}

// Instead of actually processing anything, simply accumulates all instructions
// in members that can then be accessed. TODO(skym): If this ends up being
// useful for other model type unittests it should be moved out to a shared
// location.
class FakeModelTypeChangeProcessor : public ModelTypeChangeProcessor {
 public:
  FakeModelTypeChangeProcessor() {}
  ~FakeModelTypeChangeProcessor() override {}

  void Put(const std::string& client_tag,
           scoped_ptr<EntityData> entity_data,
           MetadataChangeList* metadata_change_list) override {
    put_map_.insert(std::make_pair(client_tag, std::move(entity_data)));
  }

  void Delete(const std::string& client_tag,
              MetadataChangeList* metadata_change_list) override {
    delete_set_.insert(client_tag);
  }

  void OnMetadataLoaded(scoped_ptr<MetadataBatch> batch) override {
    std::swap(metadata_, batch);
  }

  const std::map<std::string, scoped_ptr<EntityData>>& put_map() const {
    return put_map_;
  }
  const std::set<std::string>& delete_set() const { return delete_set_; }
  const MetadataBatch* metadata() const { return metadata_.get(); }

 private:
  std::map<std::string, scoped_ptr<EntityData>> put_map_;
  std::set<std::string> delete_set_;
  scoped_ptr<MetadataBatch> metadata_;
};

class DeviceInfoServiceTest : public testing::Test,
                              public DeviceInfoTracker::Observer {
 protected:
  ~DeviceInfoServiceTest() override {
    // Some tests may never initialize the service.
    if (service_)
      service_->RemoveObserver(this);

    // Force all remaining (store) tasks to execute so we don't leak memory.
    base::RunLoop().RunUntilIdle();
  }

  void OnDeviceInfoChange() override { num_device_info_changed_callbacks_++; }

 protected:
  DeviceInfoServiceTest()
      : num_device_info_changed_callbacks_(0),
        store_(ModelTypeStoreTestUtil::CreateInMemoryStoreForTest()),
        local_device_(new LocalDeviceInfoProviderMock(
            "guid_1",
            "client_1",
            "Chromium 10k",
            "Chrome 10k",
            sync_pb::SyncEnums_DeviceType_TYPE_LINUX,
            "device_id")) {}

  // Initialized the service based on the current local device and store. Can
  // only be called once per run, as it passes |store_|.
  void InitializeService() {
    ASSERT_TRUE(store_);
    service_.reset(new DeviceInfoService(
        local_device_.get(),
        base::Bind(&ModelTypeStoreTestUtil::MoveStoreToCallback,
                   base::Passed(&store_))));
    service_->AddObserver(this);
  }

  // Creates the service and runs any outstanding tasks. This will typically
  // cause all initialization callbacks between the sevice and store to fire.
  void InitializeAndPump() {
    InitializeService();
    base::RunLoop().RunUntilIdle();
  }

  void SetProcessorAndPump() {
    processor_ = new FakeModelTypeChangeProcessor();
    service()->set_change_processor(make_scoped_ptr(processor_));
    base::RunLoop().RunUntilIdle();
  }

  DeviceInfoSpecifics GenerateTestSpecifics() {
    int label = ++generated_count_;
    DeviceInfoSpecifics specifics;
    specifics.set_cache_guid(base::StringPrintf("cache guid %d", label));
    specifics.set_client_name(base::StringPrintf("client name %d", label));
    specifics.set_device_type(sync_pb::SyncEnums_DeviceType_TYPE_LINUX);
    specifics.set_sync_user_agent(
        base::StringPrintf("sync user agent %d", label));
    specifics.set_chrome_version(
        base::StringPrintf("chrome version %d", label));
    specifics.set_signin_scoped_device_id(
        base::StringPrintf("signin scoped device id %d", label));
    return specifics;
  }

  // Allows access to the store before that will ultimately be used to
  // initialize the service.
  ModelTypeStore* store() {
    EXPECT_TRUE(store_);
    return store_.get();
  }

  // Get the number of times the service notifies observers of changes.
  int num_device_info_changed_callbacks() {
    return num_device_info_changed_callbacks_;
  }

  // Allows overriding the provider before the service is initialized.
  void set_local_device(scoped_ptr<LocalDeviceInfoProviderMock> provider) {
    ASSERT_FALSE(service_);
    std::swap(local_device_, provider);
  }
  LocalDeviceInfoProviderMock* local_device() { return local_device_.get(); }

  // Allows access to the service after InitializeService() is called.
  DeviceInfoService* service() {
    EXPECT_TRUE(service_);
    return service_.get();
  }

  FakeModelTypeChangeProcessor* processor() {
    EXPECT_TRUE(processor_);
    return processor_;
  }

 private:
  int num_device_info_changed_callbacks_;

  // Although we never use this in this class, the in memory model type store
  // grabs the current task runner from a static accessor which point at this
  // message loop. Must be declared/initilized before we call the synchronous
  // CreateInMemoryStoreForTest.
  base::MessageLoop message_loop_;

  // Temporarily holds the store before the service is initialized.
  scoped_ptr<ModelTypeStore> store_;

  scoped_ptr<LocalDeviceInfoProviderMock> local_device_;

  // Not initialized immediately (upon test's constructor). This allows each
  // test case to modify the dependencies the service will be constructed with.
  scoped_ptr<DeviceInfoService> service_;

  // A non-owning pointer to the processor given to the service. Will be nullptr
  // before being given to the service, to make ownership easier.
  FakeModelTypeChangeProcessor* processor_ = nullptr;

  // A monotonically increasing label for generated specifics objects with data
  // that is slightly different from eachother.
  int generated_count_ = 0;
};

TEST_F(DeviceInfoServiceTest, EmptyDataReconciliation) {
  InitializeService();
  ASSERT_EQ(0u, service()->GetAllDeviceInfo().size());
  base::RunLoop().RunUntilIdle();
  // TODO(skym): crbug.com/582460: Verify reconciliation has happened.
}

TEST_F(DeviceInfoServiceTest, LocalProviderSubscription) {
  set_local_device(make_scoped_ptr(new LocalDeviceInfoProviderMock()));
  InitializeAndPump();
  ASSERT_EQ(0u, service()->GetAllDeviceInfo().size());
  local_device()->Initialize(make_scoped_ptr(
      new DeviceInfo("guid_1", "client_1", "Chromium 10k", "Chrome 10k",
                     sync_pb::SyncEnums_DeviceType_TYPE_LINUX, "device_id")));
  // TODO(skym): crbug.com/582460: Verify reconciliation has happened.
}

TEST_F(DeviceInfoServiceTest, NonEmptyStoreLoad) {
  // Override the provider so that reconciliation never happens.
  set_local_device(make_scoped_ptr(new LocalDeviceInfoProviderMock()));

  scoped_ptr<WriteBatch> batch = store()->CreateWriteBatch();
  DeviceInfoSpecifics specifics(GenerateTestSpecifics());
  store()->WriteData(batch.get(), "tag", specifics.SerializeAsString());
  store()->CommitWriteBatch(std::move(batch),
                            base::Bind(&AssertResultIsSuccess));

  InitializeAndPump();

  ScopedVector<DeviceInfo> all_device_info(service()->GetAllDeviceInfo());
  ASSERT_EQ(1u, all_device_info.size());
  AssertEqual(specifics, *all_device_info[0]);
  AssertEqual(specifics, *service()->GetDeviceInfo("tag").get());
}

TEST_F(DeviceInfoServiceTest, GetClientTagNormal) {
  InitializeAndPump();
  const std::string guid = "abc";
  EntitySpecifics entity_specifics;
  entity_specifics.mutable_device_info()->set_cache_guid(guid);
  EntityData entity_data;
  entity_data.specifics = entity_specifics;
  EXPECT_EQ(guid, service()->GetClientTag(entity_data));
}

TEST_F(DeviceInfoServiceTest, GetClientTagEmpty) {
  InitializeAndPump();
  EntitySpecifics entity_specifics;
  entity_specifics.mutable_device_info();
  EntityData entity_data;
  entity_data.specifics = entity_specifics;
  EXPECT_EQ("", service()->GetClientTag(entity_data));
}

TEST_F(DeviceInfoServiceTest, TestInitStoreThenProc) {
  scoped_ptr<WriteBatch> batch = store()->CreateWriteBatch();
  DeviceInfoSpecifics specifics(GenerateTestSpecifics());
  store()->WriteData(batch.get(), "tag", specifics.SerializeAsString());
  DataTypeState state;
  state.set_encryption_key_name("ekn");
  store()->WriteGlobalMetadata(batch.get(), state.SerializeAsString());
  store()->CommitWriteBatch(std::move(batch),
                            base::Bind(&AssertResultIsSuccess));

  InitializeAndPump();

  // Verify that we have data. We do this because we're testing that the service
  // may sometimes come up after our store init is fully completed.
  ScopedVector<DeviceInfo> all_device_info(service()->GetAllDeviceInfo());
  ASSERT_EQ(1u, all_device_info.size());
  AssertEqual(specifics, *all_device_info[0]);
  AssertEqual(specifics, *service()->GetDeviceInfo("tag").get());

  SetProcessorAndPump();
  ASSERT_TRUE(processor()->metadata());
  ASSERT_EQ(state.encryption_key_name(),
            processor()->metadata()->GetDataTypeState().encryption_key_name());
}

TEST_F(DeviceInfoServiceTest, TestInitProcBeforeStoreFinishes) {
  scoped_ptr<WriteBatch> batch = store()->CreateWriteBatch();
  DeviceInfoSpecifics specifics(GenerateTestSpecifics());
  store()->WriteData(batch.get(), "tag", specifics.SerializeAsString());
  DataTypeState state;
  state.set_encryption_key_name("ekn");
  store()->WriteGlobalMetadata(batch.get(), state.SerializeAsString());
  store()->CommitWriteBatch(std::move(batch),
                            base::Bind(&AssertResultIsSuccess));

  InitializeService();
  // Verify we have _NO_ data yet, to verify that we're testing when the
  // processor is attached and ready before our store init is fully completed.
  ASSERT_EQ(0u, service()->GetAllDeviceInfo().size());

  SetProcessorAndPump();
  ASSERT_TRUE(processor()->metadata());
  ASSERT_EQ(state.encryption_key_name(),
            processor()->metadata()->GetDataTypeState().encryption_key_name());
}

TEST_F(DeviceInfoServiceTest, GetData) {
  scoped_ptr<WriteBatch> batch = store()->CreateWriteBatch();
  DeviceInfoSpecifics specifics1(GenerateTestSpecifics());
  DeviceInfoSpecifics specifics3(GenerateTestSpecifics());
  store()->WriteData(batch.get(), "tag1", specifics1.SerializeAsString());
  store()->WriteData(batch.get(), "tag2",
                     GenerateTestSpecifics().SerializeAsString());
  store()->WriteData(batch.get(), "tag3", specifics3.SerializeAsString());
  store()->CommitWriteBatch(std::move(batch),
                            base::Bind(&AssertResultIsSuccess));

  InitializeAndPump();

  std::map<std::string, DeviceInfoSpecifics> expected;
  expected["tag1"] = specifics1;
  expected["tag3"] = specifics3;
  ClientTagList client_tags;
  client_tags.push_back("tag1");
  client_tags.push_back("tag3");
  service()->GetData(client_tags,
                     base::Bind(&AssertExpectedFromDataBatch, expected));
}

TEST_F(DeviceInfoServiceTest, GetDataMissing) {
  InitializeAndPump();
  std::map<std::string, DeviceInfoSpecifics> expected;
  ClientTagList client_tags;
  client_tags.push_back("tag1");
  service()->GetData(client_tags,
                     base::Bind(&AssertExpectedFromDataBatch, expected));
}

TEST_F(DeviceInfoServiceTest, GetDataNotInitialized) {
  InitializeService();
  ClientTagList client_tags;
  service()->GetData(client_tags, base::Bind(&AssertErrorFromDataBatch));
}

TEST_F(DeviceInfoServiceTest, GetAllData) {
  scoped_ptr<WriteBatch> batch = store()->CreateWriteBatch();
  DeviceInfoSpecifics specifics1(GenerateTestSpecifics());
  DeviceInfoSpecifics specifics2(GenerateTestSpecifics());
  store()->WriteData(batch.get(), "tag1", specifics1.SerializeAsString());
  store()->WriteData(batch.get(), "tag2", specifics2.SerializeAsString());
  store()->CommitWriteBatch(std::move(batch),
                            base::Bind(&AssertResultIsSuccess));

  InitializeAndPump();

  std::map<std::string, DeviceInfoSpecifics> expected;
  expected["tag1"] = specifics1;
  expected["tag2"] = specifics2;
  ClientTagList client_tags;
  client_tags.push_back("tag1");
  client_tags.push_back("tag2");
  service()->GetData(client_tags,
                     base::Bind(&AssertExpectedFromDataBatch, expected));
}

TEST_F(DeviceInfoServiceTest, GetAllDataNotInitialized) {
  InitializeService();
  service()->GetAllData(base::Bind(&AssertErrorFromDataBatch));
}

}  // namespace

}  // namespace sync_driver_v2
