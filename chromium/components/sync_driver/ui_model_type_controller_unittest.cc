// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_driver/ui_model_type_controller.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/test/test_simple_task_runner.h"
#include "base/threading/thread.h"
#include "components/sync_driver/backend_data_type_configurer.h"
#include "components/sync_driver/fake_sync_client.h"
#include "sync/api/fake_model_type_service.h"
#include "sync/engine/commit_queue.h"
#include "sync/internal_api/public/activation_context.h"
#include "sync/internal_api/public/shared_model_type_processor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace sync_driver_v2 {

namespace {

// Test controller derived from UIModelTypeController.
class TestUIModelTypeController : public UIModelTypeController {
 public:
  TestUIModelTypeController(
      const scoped_refptr<base::SingleThreadTaskRunner>& ui_thread,
      const base::Closure& error_callback,
      syncer::ModelType model_type,
      sync_driver::SyncClient* sync_client)
      : UIModelTypeController(ui_thread,
                              error_callback,
                              model_type,
                              sync_client) {}

 private:
  ~TestUIModelTypeController() override {}
};

// A no-op instance of CommitQueue.
class NullCommitQueue : public syncer_v2::CommitQueue {
 public:
  NullCommitQueue() {}
  ~NullCommitQueue() override {}

  void EnqueueForCommit(const syncer_v2::CommitRequestDataList& list) override {
    NOTREACHED() << "Not implemented.";
  }
};

// A class that pretends to be the sync backend.
class MockSyncBackend {
 public:
  void Connect(
      syncer::ModelType type,
      std::unique_ptr<syncer_v2::ActivationContext> activation_context) {
    enabled_types_.Put(type);
    activation_context->type_processor->ConnectSync(
        base::WrapUnique(new NullCommitQueue()));
  }

  void Disconnect(syncer::ModelType type) {
    DCHECK(enabled_types_.Has(type));
    enabled_types_.Remove(type);
  }

 private:
  syncer::ModelTypeSet enabled_types_;
};

// Fake implementation of BackendDataTypeConfigurer that pretends to be Sync
// backend.
class MockBackendDataTypeConfigurer
    : public sync_driver::BackendDataTypeConfigurer {
 public:
  MockBackendDataTypeConfigurer(
      MockSyncBackend* backend,
      const scoped_refptr<base::TaskRunner>& sync_task_runner)
      : backend_(backend), sync_task_runner_(sync_task_runner) {}
  ~MockBackendDataTypeConfigurer() override {}

  syncer::ModelTypeSet ConfigureDataTypes(
      syncer::ConfigureReason reason,
      const DataTypeConfigStateMap& config_state_map,
      const base::Callback<void(syncer::ModelTypeSet, syncer::ModelTypeSet)>&
          ready_task,
      const base::Callback<void()>& retry_callback) override {
    NOTREACHED() << "Not implemented.";
    return syncer::ModelTypeSet();
  }

  void ActivateDirectoryDataType(
      syncer::ModelType type,
      syncer::ModelSafeGroup group,
      sync_driver::ChangeProcessor* change_processor) override {
    NOTREACHED() << "Not implemented.";
  }

  void DeactivateDirectoryDataType(syncer::ModelType type) override {
    NOTREACHED() << "Not implemented.";
  }

  void ActivateNonBlockingDataType(syncer::ModelType type,
                                   std::unique_ptr<syncer_v2::ActivationContext>
                                       activation_context) override {
    // Post on Sync thread just like the real implementation does.
    sync_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&MockSyncBackend::Connect, base::Unretained(backend_), type,
                   base::Passed(std::move(activation_context))));
  }

  void DeactivateNonBlockingDataType(syncer::ModelType type) override {
    sync_task_runner_->PostTask(FROM_HERE,
                                base::Bind(&MockSyncBackend::Disconnect,
                                           base::Unretained(backend_), type));
  }

 private:
  MockSyncBackend* backend_;
  scoped_refptr<base::TaskRunner> sync_task_runner_;
};

}  // namespace

class UIModelTypeControllerTest : public testing::Test,
                                  public sync_driver::FakeSyncClient {
 public:
  UIModelTypeControllerTest()
      : auto_run_tasks_(true),
        load_models_callback_called_(false),
        association_callback_called_(false),
        sync_thread_runner_(new base::TestSimpleTaskRunner()),
        configurer_(&backend_, sync_thread_runner_) {}

  ~UIModelTypeControllerTest() override {}

  void SetUp() override {
    controller_ = new TestUIModelTypeController(
        ui_loop_.task_runner(), base::Closure(), syncer::DEVICE_INFO, this);
    service_.reset(new syncer_v2::FakeModelTypeService(base::Bind(
        &UIModelTypeControllerTest::CreateProcessor, base::Unretained(this))));
  }

  void TearDown() override {
    controller_ = NULL;
    RunQueuedUIThreadTasks();
  }

  syncer_v2::ModelTypeService* GetModelTypeServiceForType(
      syncer::ModelType type) override {
    return service_.get();
  }

 protected:
  std::unique_ptr<syncer_v2::ModelTypeChangeProcessor> CreateProcessor(
      syncer::ModelType type,
      syncer_v2::ModelTypeService* service) {
    std::unique_ptr<syncer_v2::SharedModelTypeProcessor> processor =
        base::WrapUnique(
            new syncer_v2::SharedModelTypeProcessor(type, service));
    type_processor_ = processor.get();
    return std::move(processor);
  }

  void ExpectProcessorConnected(bool isConnected) {
    DCHECK(type_processor_);
    EXPECT_EQ(isConnected, type_processor_->IsConnected());
  }

  void LoadModels() {
    controller_->LoadModels(base::Bind(
        &UIModelTypeControllerTest::LoadModelsDone, base::Unretained(this)));
    if (!type_processor_->IsAllowingChanges()) {
      type_processor_->OnMetadataLoaded(
          syncer::SyncError(),
          base::WrapUnique(new syncer_v2::MetadataBatch()));
    }

    if (auto_run_tasks_) {
      RunAllTasks();
    }
  }

  void RegisterWithBackend() {
    controller_->RegisterWithBackend(&configurer_);
    if (auto_run_tasks_) {
      RunAllTasks();
    }
  }

  void StartAssociating() {
    controller_->StartAssociating(base::Bind(
        &UIModelTypeControllerTest::AssociationDone, base::Unretained(this)));
    // The callback is expected to be promptly called.
    EXPECT_TRUE(association_callback_called_);
  }

  void DeactivateDataTypeAndStop() {
    controller_->DeactivateDataType(&configurer_);
    controller_->Stop();
    if (auto_run_tasks_) {
      RunAllTasks();
    }
  }

  // These threads can ping-pong for a bit so we run the UI thread twice.
  void RunAllTasks() {
    RunQueuedUIThreadTasks();
    RunQueuedSyncThreadTasks();
    RunQueuedUIThreadTasks();
  }

  // Runs any tasks posted on UI thread.
  void RunQueuedUIThreadTasks() { ui_loop_.RunUntilIdle(); }

  // Processes any pending connect or disconnect requests and sends
  // responses synchronously.
  void RunQueuedSyncThreadTasks() { sync_thread_runner_->RunUntilIdle(); }

  void SetAutoRunTasks(bool auto_run_tasks) {
    auto_run_tasks_ = auto_run_tasks;
  }

  void LoadModelsDone(syncer::ModelType type, syncer::SyncError error) {
    load_models_callback_called_ = true;
    load_models_error_ = error;
  }

  void AssociationDone(sync_driver::DataTypeController::ConfigureResult result,
                       const syncer::SyncMergeResult& local_merge_result,
                       const syncer::SyncMergeResult& syncer_merge_result) {
    EXPECT_EQ(sync_driver::DataTypeController::OK, result);
    association_callback_called_ = true;
  }

  syncer_v2::SharedModelTypeProcessor* type_processor_;
  scoped_refptr<TestUIModelTypeController> controller_;

  bool auto_run_tasks_;
  bool load_models_callback_called_;
  syncer::SyncError load_models_error_;
  bool association_callback_called_;
  base::MessageLoopForUI ui_loop_;
  scoped_refptr<base::TestSimpleTaskRunner> sync_thread_runner_;
  MockSyncBackend backend_;
  MockBackendDataTypeConfigurer configurer_;
  std::unique_ptr<syncer_v2::FakeModelTypeService> service_;
};

TEST_F(UIModelTypeControllerTest, InitialState) {
  EXPECT_EQ(syncer::DEVICE_INFO, controller_->type());
  EXPECT_EQ(sync_driver::DataTypeController::NOT_RUNNING, controller_->state());
}

TEST_F(UIModelTypeControllerTest, LoadModelsOnUIThread) {
  LoadModels();
  EXPECT_EQ(sync_driver::DataTypeController::MODEL_LOADED,
            controller_->state());
  EXPECT_TRUE(load_models_callback_called_);
  EXPECT_FALSE(load_models_error_.IsSet());
  ExpectProcessorConnected(false);
}

TEST_F(UIModelTypeControllerTest, LoadModelsTwice) {
  LoadModels();
  SetAutoRunTasks(false);
  LoadModels();
  EXPECT_EQ(sync_driver::DataTypeController::MODEL_LOADED,
            controller_->state());
  // The second LoadModels call should set the error.
  EXPECT_TRUE(load_models_error_.IsSet());
}

TEST_F(UIModelTypeControllerTest, ActivateDataTypeOnUIThread) {
  LoadModels();
  EXPECT_EQ(sync_driver::DataTypeController::MODEL_LOADED,
            controller_->state());
  RegisterWithBackend();
  ExpectProcessorConnected(true);

  StartAssociating();
  EXPECT_EQ(sync_driver::DataTypeController::RUNNING, controller_->state());
}

TEST_F(UIModelTypeControllerTest, Stop) {
  LoadModels();
  RegisterWithBackend();
  ExpectProcessorConnected(true);
  StartAssociating();

  DeactivateDataTypeAndStop();
  EXPECT_EQ(sync_driver::DataTypeController::NOT_RUNNING, controller_->state());
}

}  // namespace sync_driver_v2
