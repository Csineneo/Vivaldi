// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/driver/shared_change_processor.h"

#include <cstddef>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "components/sync/api/attachments/attachment_id.h"
#include "components/sync/api/attachments/attachment_store.h"
#include "components/sync/api/data_type_error_handler_mock.h"
#include "components/sync/api/fake_syncable_service.h"
#include "components/sync/base/model_type.h"
#include "components/sync/core/attachments/attachment_service_impl.h"
#include "components/sync/core/test/test_user_share.h"
#include "components/sync/device_info/local_device_info_provider.h"
#include "components/sync/driver/fake_sync_client.h"
#include "components/sync/driver/generic_change_processor.h"
#include "components/sync/driver/generic_change_processor_factory.h"
#include "components/sync/driver/sync_api_component_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using ::testing::NiceMock;
using ::testing::StrictMock;

class TestSyncApiComponentFactory : public SyncApiComponentFactory {
 public:
  TestSyncApiComponentFactory() {}
  ~TestSyncApiComponentFactory() override {}

  // SyncApiComponentFactory implementation.
  void RegisterDataTypes(
      SyncService* sync_service,
      const RegisterDataTypesMethod& register_platform_types_method) override {}
  DataTypeManager* CreateDataTypeManager(
      const WeakHandle<DataTypeDebugInfoListener>& debug_info_listener,
      const DataTypeController::TypeMap* controllers,
      const DataTypeEncryptionHandler* encryption_handler,
      SyncBackendHost* backend,
      DataTypeManagerObserver* observer) override {
    return nullptr;
  }
  SyncBackendHost* CreateSyncBackendHost(
      const std::string& name,
      invalidation::InvalidationService* invalidator,
      const base::WeakPtr<SyncPrefs>& sync_prefs,
      const base::FilePath& sync_folder) override {
    return nullptr;
  }
  std::unique_ptr<LocalDeviceInfoProvider> CreateLocalDeviceInfoProvider()
      override {
    return nullptr;
  }
  SyncApiComponentFactory::SyncComponents CreateBookmarkSyncComponents(
      SyncService* sync_service,
      std::unique_ptr<DataTypeErrorHandler> error_handler) override {
    return SyncApiComponentFactory::SyncComponents(nullptr, nullptr);
  }
  std::unique_ptr<AttachmentService> CreateAttachmentService(
      std::unique_ptr<AttachmentStoreForSync> attachment_store,
      const UserShare& user_share,
      const std::string& store_birthday,
      ModelType model_type,
      AttachmentService::Delegate* delegate) override {
    return AttachmentServiceImpl::CreateForTest();
  }
};

class SyncSharedChangeProcessorTest : public testing::Test,
                                      public FakeSyncClient {
 public:
  SyncSharedChangeProcessorTest()
      : FakeSyncClient(&factory_),
        backend_thread_("dbthread"),
        did_connect_(false),
        has_attachment_service_(false) {}

  ~SyncSharedChangeProcessorTest() override {
    EXPECT_FALSE(db_syncable_service_.get());
  }

  // FakeSyncClient override.
  base::WeakPtr<SyncableService> GetSyncableServiceForType(
      ModelType type) override {
    return db_syncable_service_->AsWeakPtr();
  }

 protected:
  void SetUp() override {
    test_user_share_.SetUp();
    shared_change_processor_ = new SharedChangeProcessor(AUTOFILL);
    ASSERT_TRUE(backend_thread_.Start());
    ASSERT_TRUE(backend_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&SyncSharedChangeProcessorTest::SetUpDBSyncableService,
                   base::Unretained(this))));
  }

  void TearDown() override {
    EXPECT_TRUE(backend_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&SyncSharedChangeProcessorTest::TearDownDBSyncableService,
                   base::Unretained(this))));
    // This must happen before the DB thread is stopped since
    // |shared_change_processor_| may post tasks to delete its members
    // on the correct thread.
    //
    // TODO(akalin): Write deterministic tests for the destruction of
    // |shared_change_processor_| on the UI and DB threads.
    shared_change_processor_ = NULL;
    backend_thread_.Stop();

    // Note: Stop() joins the threads, and that barrier prevents this read
    // from being moved (e.g by compiler optimization) in such a way that it
    // would race with the write in ConnectOnDBThread (because by this time,
    // everything that could have run on |backend_thread_| has done so).
    ASSERT_TRUE(did_connect_);
    test_user_share_.TearDown();
  }

  // Connect |shared_change_processor_| on the DB thread.
  void Connect() {
    EXPECT_TRUE(backend_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&SyncSharedChangeProcessorTest::ConnectOnDBThread,
                   base::Unretained(this), shared_change_processor_)));
  }

  void SetAttachmentStore() {
    EXPECT_TRUE(backend_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&SyncSharedChangeProcessorTest::SetAttachmentStoreOnDBThread,
                   base::Unretained(this))));
  }

  bool HasAttachmentService() {
    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
    EXPECT_TRUE(backend_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(
            &SyncSharedChangeProcessorTest::CheckAttachmentServiceOnDBThread,
            base::Unretained(this), base::Unretained(&event))));
    event.Wait();
    return has_attachment_service_;
  }

 private:
  // Used by SetUp().
  void SetUpDBSyncableService() {
    DCHECK(backend_thread_.task_runner()->BelongsToCurrentThread());
    DCHECK(!db_syncable_service_.get());
    db_syncable_service_.reset(new FakeSyncableService());
  }

  // Used by TearDown().
  void TearDownDBSyncableService() {
    DCHECK(backend_thread_.task_runner()->BelongsToCurrentThread());
    DCHECK(db_syncable_service_.get());
    db_syncable_service_.reset();
  }

  void SetAttachmentStoreOnDBThread() {
    DCHECK(backend_thread_.task_runner()->BelongsToCurrentThread());
    DCHECK(db_syncable_service_.get());
    db_syncable_service_->set_attachment_store(
        AttachmentStore::CreateInMemoryStore());
  }

  // Used by Connect().  The SharedChangeProcessor is passed in
  // because we modify |shared_change_processor_| on the main thread
  // (in TearDown()).
  void ConnectOnDBThread(
      const scoped_refptr<SharedChangeProcessor>& shared_change_processor) {
    DCHECK(backend_thread_.task_runner()->BelongsToCurrentThread());
    EXPECT_TRUE(shared_change_processor->Connect(
        this, &processor_factory_, test_user_share_.user_share(),
        base::MakeUnique<DataTypeErrorHandlerMock>(),
        base::WeakPtr<SyncMergeResult>()));
    did_connect_ = true;
  }

  void CheckAttachmentServiceOnDBThread(base::WaitableEvent* event) {
    DCHECK(backend_thread_.task_runner()->BelongsToCurrentThread());
    DCHECK(db_syncable_service_.get());
    has_attachment_service_ = !!db_syncable_service_->attachment_service();
    event->Signal();
  }

  base::MessageLoop frontend_loop_;
  base::Thread backend_thread_;
  TestUserShare test_user_share_;
  TestSyncApiComponentFactory factory_;

  scoped_refptr<SharedChangeProcessor> shared_change_processor_;

  GenericChangeProcessorFactory processor_factory_;
  bool did_connect_;
  bool has_attachment_service_;

  // Used only on DB thread.
  std::unique_ptr<FakeSyncableService> db_syncable_service_;
};

// Simply connect the shared change processor.  It should succeed, and
// nothing further should happen.
TEST_F(SyncSharedChangeProcessorTest, Basic) {
  Connect();
}

// Connect the shared change processor to a syncable service with
// AttachmentStore. Verify that shared change processor implementation
// creates AttachmentService and passes it back to the syncable service.
TEST_F(SyncSharedChangeProcessorTest, ConnectWithAttachmentStore) {
  SetAttachmentStore();
  Connect();
  EXPECT_TRUE(HasAttachmentService());
}

}  // namespace

}  // namespace syncer
