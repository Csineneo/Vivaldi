// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_NON_UI_DATA_TYPE_CONTROLLER_H_
#define COMPONENTS_SYNC_DRIVER_NON_UI_DATA_TYPE_CONTROLLER_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "components/sync/driver/directory_data_type_controller.h"
#include "components/sync/driver/shared_change_processor.h"

namespace syncer {

class SyncClient;
class SyncableService;
struct UserShare;

class NonUIDataTypeController : public DirectoryDataTypeController {
 public:
  // |dump_stack| is called when an unrecoverable error occurs.
  NonUIDataTypeController(ModelType type,
                          const base::Closure& dump_stack,
                          SyncClient* sync_client);
  ~NonUIDataTypeController() override;

  // DataTypeController interface.
  void LoadModels(const ModelLoadCallback& model_load_callback) override;
  void StartAssociating(const StartCallback& start_callback) override;
  void Stop() override;
  ModelSafeGroup model_safe_group() const override = 0;
  ChangeProcessor* GetChangeProcessor() const override;
  std::string name() const override;
  State state() const override;

 protected:
  // For testing only.
  NonUIDataTypeController();

  // Start any dependent services that need to be running before we can
  // associate models. The default implementation is a no-op.
  // Return value:
  //   True - if models are ready and association can proceed.
  //   False - if models are not ready. StartAssociationAsync should be called
  //           when the models are ready.
  // Note: this is performed on the UI thread.
  virtual bool StartModels();

  // Perform any DataType controller specific state cleanup before stopping
  // the datatype controller. The default implementation is a no-op.
  // Note: this is performed on the UI thread.
  virtual void StopModels();

  // Posts the given task to the backend thread, i.e. the thread the
  // datatype lives on.  Return value: True if task posted successfully,
  // false otherwise.
  virtual bool PostTaskOnBackendThread(
      const tracked_objects::Location& from_here,
      const base::Closure& task) = 0;

  // Start up complete, update the state and invoke the callback.
  virtual void StartDone(DataTypeController::ConfigureResult start_result,
                         const SyncMergeResult& local_merge_result,
                         const SyncMergeResult& syncer_merge_result);

  // Kick off the association process.
  virtual bool StartAssociationAsync();

  // Record causes of start failure.
  virtual void RecordStartFailure(ConfigureResult result);

  // To allow unit tests to control thread interaction during non-ui startup
  // and shutdown, use a factory method to create the SharedChangeProcessor.
  virtual SharedChangeProcessor* CreateSharedChangeProcessor();

  // If the DTC is waiting for models to load, once the models are
  // loaded the datatype service will call this function on DTC to let
  // us know that it is safe to start associating.
  void OnModelLoaded();

  std::unique_ptr<DataTypeErrorHandler> CreateErrorHandler() override;

 private:
  // Posted on the backend thread by StartAssociationAsync().
  void StartAssociationWithSharedChangeProcessor(
      const scoped_refptr<SharedChangeProcessor>& shared_change_processor);

  // Calls Disconnect() on |shared_change_processor_|, then sets it to
  // NULL.  Must be called only by StartDoneImpl() or Stop() (on the
  // UI thread) and only after a call to Start() (i.e.,
  // |shared_change_processor_| must be non-NULL).
  void DisconnectSharedChangeProcessor();

  // Posts StopLocalService() to the processor on the model type thread.
  void StopSyncableService();

  // Disable this type with the sync service. Should only be invoked in case of
  // an unrecoverable error.
  // Note: this is performed on the UI thread.
  void DisableImpl(const SyncError& error);

  // UserShare is stored in StartAssociating while on UI thread and
  // passed to SharedChangeProcessor::Connect on the model thread.
  UserShare* user_share_;

  // State of this datatype controller.
  State state_;

  // Callbacks for use when starting the datatype.
  StartCallback start_callback_;
  ModelLoadCallback model_load_callback_;

  // The shared change processor is the thread-safe interface to the
  // datatype.  We hold a reference to it from the UI thread so that
  // we can call Disconnect() on it from Stop()/StartDoneImpl().  Most
  // of the work is done on the backend thread, and in
  // StartAssociationWithSharedChangeProcessor() for this class in
  // particular.
  //
  // Lifetime: The SharedChangeProcessor object is created on the UI
  // thread and passed on to the backend thread.  This reference is
  // released on the UI thread in Stop()/StartDoneImpl(), but the
  // backend thread may still have references to it (which is okay,
  // since we call Disconnect() before releasing the UI thread
  // reference).
  scoped_refptr<SharedChangeProcessor> shared_change_processor_;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_DRIVER_NON_UI_DATA_TYPE_CONTROLLER_H_
