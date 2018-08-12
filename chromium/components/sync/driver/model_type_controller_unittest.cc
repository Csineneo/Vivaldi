// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/driver/model_type_controller.h"

#include <utility>

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
#include "base/threading/thread_task_runner_handle.h"
#include "components/sync/api/fake_model_type_change_processor.h"
#include "components/sync/api/stub_model_type_service.h"
#include "components/sync/core/activation_context.h"
#include "components/sync/core/model_type_processor_proxy.h"
#include "components/sync/core/shared_model_type_processor.h"
#include "components/sync/core/test/fake_model_type_processor.h"
#include "components/sync/driver/backend_data_type_configurer.h"
#include "components/sync/driver/fake_sync_client.h"
#include "components/sync/engine/commit_queue.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

const ModelType kTestModelType = AUTOFILL;

// A change processor for testing that connects using a thread-jumping proxy,
// tracks connected state, and counts DisableSync calls.
class TestModelTypeProcessor : public FakeModelTypeChangeProcessor,
                               public FakeModelTypeProcessor {
 public:
  explicit TestModelTypeProcessor(int* disable_sync_call_count)
      : disable_sync_call_count_(disable_sync_call_count),
        weak_factory_(this) {}

  // ModelTypeChangeProcessor implementation.
  void OnSyncStarting(std::unique_ptr<DataTypeErrorHandler> error_handler,
                      const StartCallback& callback) override {
    std::unique_ptr<ActivationContext> activation_context =
        base::MakeUnique<ActivationContext>();
    activation_context->type_processor =
        base::MakeUnique<ModelTypeProcessorProxy>(
            weak_factory_.GetWeakPtr(), base::ThreadTaskRunnerHandle::Get());
    callback.Run(SyncError(), std::move(activation_context));
  }
  void DisableSync() override { (*disable_sync_call_count_)++; }

  // ModelTypeProcessor implementation.
  void ConnectSync(std::unique_ptr<CommitQueue> commit_queue) override {
    is_connected_ = true;
  }
  void DisconnectSync() override { is_connected_ = false; }

  bool is_connected() { return is_connected_; }

 private:
  bool is_connected_ = false;
  int* disable_sync_call_count_;
  base::WeakPtrFactory<TestModelTypeProcessor> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(TestModelTypeProcessor);
};

// A BackendDataTypeConfigurer that just connects USS types.
class TestBackendDataTypeConfigurer : public BackendDataTypeConfigurer {
 public:
  TestBackendDataTypeConfigurer() {}
  ~TestBackendDataTypeConfigurer() override {}

  ModelTypeSet ConfigureDataTypes(
      ConfigureReason reason,
      const DataTypeConfigStateMap& config_state_map,
      const base::Callback<void(ModelTypeSet, ModelTypeSet)>& ready_task,
      const base::Callback<void()>& retry_callback) override {
    NOTREACHED() << "Not implemented.";
    return ModelTypeSet();
  }

  void ActivateDirectoryDataType(ModelType type,
                                 ModelSafeGroup group,
                                 ChangeProcessor* change_processor) override {
    NOTREACHED() << "Not implemented.";
  }

  void DeactivateDirectoryDataType(ModelType type) override {
    NOTREACHED() << "Not implemented.";
  }

  void ActivateNonBlockingDataType(
      ModelType type,
      std::unique_ptr<ActivationContext> activation_context) override {
    DCHECK_EQ(kTestModelType, type);
    DCHECK(!processor_);
    processor_ = std::move(activation_context->type_processor);
    processor_->ConnectSync(nullptr);
  }

  void DeactivateNonBlockingDataType(ModelType type) override {
    DCHECK_EQ(kTestModelType, type);
    DCHECK(processor_);
    processor_->DisconnectSync();
    processor_.reset();
  }

 private:
  std::unique_ptr<ModelTypeProcessor> processor_;
};

}  // namespace

class ModelTypeControllerTest : public testing::Test, public FakeSyncClient {
 public:
  ModelTypeControllerTest()
      : model_thread_("modelthread"), sync_prefs_(GetPrefService()) {}

  void SetUp() override {
    model_thread_.Start();
    InitializeModelTypeService();
    controller_.reset(new ModelTypeController(
        kTestModelType, base::Closure(), this, model_thread_.task_runner()));
  }

  void TearDown() override {
    ClearModelTypeService();
    PumpUIThread();
  }

  base::WeakPtr<ModelTypeService> GetModelTypeServiceForType(
      ModelType type) override {
    return service_->AsWeakPtr();
  }

  void LoadModels() {
    controller_->LoadModels(base::Bind(&ModelTypeControllerTest::LoadModelsDone,
                                       base::Unretained(this)));
  }

  void RegisterWithBackend() { controller_->RegisterWithBackend(&configurer_); }

  void StartAssociating() {
    controller_->StartAssociating(base::Bind(
        &ModelTypeControllerTest::AssociationDone, base::Unretained(this)));
    // The callback is expected to be promptly called.
    EXPECT_TRUE(association_callback_called_);
  }

  void DeactivateDataTypeAndStop() {
    controller_->DeactivateDataType(&configurer_);
    controller_->Stop();
  }

  // These threads can ping-pong for a bit so we run the model thread twice.
  void RunAllTasks() {
    PumpModelThread();
    PumpUIThread();
    PumpModelThread();
  }

  // Runs any tasks posted on the model thread.
  void PumpModelThread() {
    base::RunLoop run_loop;
    model_thread_.task_runner()->PostTaskAndReply(
        FROM_HERE, base::Bind(&base::DoNothing), run_loop.QuitClosure());
    run_loop.Run();
  }

  void ExpectProcessorConnected(bool is_connected) {
    if (model_thread_.task_runner()->BelongsToCurrentThread()) {
      DCHECK(processor_);
      EXPECT_EQ(is_connected, processor_->is_connected());
    } else {
      model_thread_.task_runner()->PostTask(
          FROM_HERE,
          base::Bind(&ModelTypeControllerTest::ExpectProcessorConnected,
                     base::Unretained(this), is_connected));
      PumpModelThread();
    }
  }

  void ExpectHasChangeProcessor(bool has_processor) {
    if (model_thread_.task_runner()->BelongsToCurrentThread()) {
      DCHECK(service_);
      EXPECT_EQ(has_processor, !!service_->change_processor());
    } else {
      model_thread_.task_runner()->PostTask(
          FROM_HERE,
          base::Bind(&ModelTypeControllerTest::ExpectHasChangeProcessor,
                     base::Unretained(this), has_processor));
      PumpModelThread();
    }
  }

  SyncPrefs* sync_prefs() { return &sync_prefs_; }
  DataTypeController* controller() { return controller_.get(); }
  int load_models_done_count() { return load_models_done_count_; }
  int disable_sync_call_count() { return disable_sync_call_count_; }
  SyncError load_models_last_error() { return load_models_last_error_; }

 private:
  // Runs any tasks posted on the UI thread.
  void PumpUIThread() { base::RunLoop().RunUntilIdle(); }

  void LoadModelsDone(ModelType type, const SyncError& error) {
    load_models_done_count_++;
    load_models_last_error_ = error;
  }

  void AssociationDone(DataTypeController::ConfigureResult result,
                       const SyncMergeResult& local_merge_result,
                       const SyncMergeResult& syncer_merge_result) {
    EXPECT_FALSE(association_callback_called_);
    EXPECT_EQ(DataTypeController::OK, result);
    association_callback_called_ = true;
  }

  std::unique_ptr<ModelTypeChangeProcessor> CreateProcessor(
      ModelType type,
      ModelTypeService* service) {
    std::unique_ptr<TestModelTypeProcessor> processor =
        base::MakeUnique<TestModelTypeProcessor>(&disable_sync_call_count_);
    processor_ = processor.get();
    return std::move(processor);
  }

  void InitializeModelTypeService() {
    if (model_thread_.task_runner()->BelongsToCurrentThread()) {
      service_.reset(new StubModelTypeService(base::Bind(
          &ModelTypeControllerTest::CreateProcessor, base::Unretained(this))));
    } else {
      model_thread_.task_runner()->PostTask(
          FROM_HERE,
          base::Bind(&ModelTypeControllerTest::InitializeModelTypeService,
                     base::Unretained(this)));
      PumpModelThread();
    }
  }

  void ClearModelTypeService() {
    if (model_thread_.task_runner()->BelongsToCurrentThread()) {
      service_.reset();
    } else {
      model_thread_.task_runner()->PostTask(
          FROM_HERE, base::Bind(&ModelTypeControllerTest::ClearModelTypeService,
                                base::Unretained(this)));
      PumpModelThread();
    }
  }

  int load_models_done_count_ = 0;
  int disable_sync_call_count_ = 0;
  bool association_callback_called_ = false;
  SyncError load_models_last_error_;

  base::MessageLoop message_loop_;
  base::Thread model_thread_;
  SyncPrefs sync_prefs_;
  TestBackendDataTypeConfigurer configurer_;
  std::unique_ptr<StubModelTypeService> service_;
  std::unique_ptr<ModelTypeController> controller_;
  TestModelTypeProcessor* processor_;
};

TEST_F(ModelTypeControllerTest, InitialState) {
  EXPECT_EQ(kTestModelType, controller()->type());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());
}

TEST_F(ModelTypeControllerTest, LoadModelsOnBackendThread) {
  LoadModels();
  EXPECT_EQ(DataTypeController::MODEL_STARTING, controller()->state());
  RunAllTasks();
  EXPECT_EQ(DataTypeController::MODEL_LOADED, controller()->state());
  EXPECT_EQ(1, load_models_done_count());
  EXPECT_FALSE(load_models_last_error().IsSet());
  ExpectProcessorConnected(false);
}

TEST_F(ModelTypeControllerTest, LoadModelsTwice) {
  LoadModels();
  RunAllTasks();
  LoadModels();
  EXPECT_EQ(DataTypeController::MODEL_LOADED, controller()->state());
  // The second LoadModels call should set the error.
  EXPECT_TRUE(load_models_last_error().IsSet());
}

TEST_F(ModelTypeControllerTest, ActivateDataTypeOnBackendThread) {
  LoadModels();
  RunAllTasks();
  EXPECT_EQ(DataTypeController::MODEL_LOADED, controller()->state());
  RegisterWithBackend();
  ExpectProcessorConnected(true);

  StartAssociating();
  EXPECT_EQ(DataTypeController::RUNNING, controller()->state());
}

TEST_F(ModelTypeControllerTest, Stop) {
  LoadModels();
  RunAllTasks();
  RegisterWithBackend();
  ExpectProcessorConnected(true);

  StartAssociating();

  DeactivateDataTypeAndStop();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());
}

// Test emulates normal browser shutdown. Ensures that DisableSync is not
// called.
TEST_F(ModelTypeControllerTest, StopWhenDatatypeEnabled) {
  // Enable datatype through preferences.
  sync_prefs()->SetFirstSetupComplete();
  sync_prefs()->SetKeepEverythingSynced(true);

  LoadModels();
  RunAllTasks();
  StartAssociating();

  controller()->Stop();
  RunAllTasks();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());
  // Ensure that DisableSync is not called and service still has valid change
  // processor.
  EXPECT_EQ(0, disable_sync_call_count());
  ExpectHasChangeProcessor(true);
  ExpectProcessorConnected(false);
}

// Test emulates scenario when user disables datatype. DisableSync should be
// called.
TEST_F(ModelTypeControllerTest, StopWhenDatatypeDisabled) {
  // Enable datatype through preferences.
  sync_prefs()->SetFirstSetupComplete();
  sync_prefs()->SetKeepEverythingSynced(true);
  LoadModels();
  RunAllTasks();
  StartAssociating();

  // Disable datatype through preferences.
  sync_prefs()->SetKeepEverythingSynced(false);
  sync_prefs()->SetPreferredDataTypes(ModelTypeSet(kTestModelType),
                                      ModelTypeSet());

  controller()->Stop();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());
  // Ensure that DisableSync is called and change processor is reset.
  PumpModelThread();
  EXPECT_EQ(1, disable_sync_call_count());
  ExpectHasChangeProcessor(false);
}

// Test emulates disabling sync by signing out. DisableSync should be called.
TEST_F(ModelTypeControllerTest, StopWithInitialSyncPrefs) {
  // Enable datatype through preferences.
  sync_prefs()->SetFirstSetupComplete();
  sync_prefs()->SetKeepEverythingSynced(true);
  LoadModels();
  RunAllTasks();
  StartAssociating();

  // Clearing preferences emulates signing out.
  sync_prefs()->ClearPreferences();
  controller()->Stop();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());
  // Ensure that DisableSync is called and change processor is reset.
  PumpModelThread();
  EXPECT_EQ(1, disable_sync_call_count());
  ExpectHasChangeProcessor(false);
}

// Test emulates disabling sync when datatype is not loaded yet. DisableSync
// should not be called as service is potentially not ready to handle it.
TEST_F(ModelTypeControllerTest, StopBeforeLoadModels) {
  // Enable datatype through preferences.
  sync_prefs()->SetFirstSetupComplete();
  sync_prefs()->SetKeepEverythingSynced(true);
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());

  // Clearing preferences emulates signing out.
  sync_prefs()->ClearPreferences();
  controller()->Stop();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, controller()->state());
  // Ensure that DisableSync is not called.
  EXPECT_EQ(0, disable_sync_call_count());
  // A change processor was never created.
  ExpectHasChangeProcessor(false);
}

}  // namespace syncer
