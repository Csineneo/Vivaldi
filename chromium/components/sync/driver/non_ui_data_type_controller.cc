// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/driver/non_ui_data_type_controller.h"

#include "base/logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/sync/api/data_type_error_handler_impl.h"
#include "components/sync/api/sync_error.h"
#include "components/sync/api/sync_merge_result.h"
#include "components/sync/api/syncable_service.h"
#include "components/sync/base/bind_to_task_runner.h"
#include "components/sync/base/data_type_histogram.h"
#include "components/sync/base/model_type.h"
#include "components/sync/driver/generic_change_processor_factory.h"
#include "components/sync/driver/sync_api_component_factory.h"
#include "components/sync/driver/sync_client.h"
#include "components/sync/driver/sync_service.h"

namespace syncer {

SharedChangeProcessor* NonUIDataTypeController::CreateSharedChangeProcessor() {
  return new SharedChangeProcessor(type());
}

NonUIDataTypeController::NonUIDataTypeController(
    ModelType type,
    const base::Closure& dump_stack,
    SyncClient* sync_client)
    : DirectoryDataTypeController(type, dump_stack, sync_client),
      state_(NOT_RUNNING) {}

void NonUIDataTypeController::LoadModels(
    const ModelLoadCallback& model_load_callback) {
  DCHECK(CalledOnValidThread());
  model_load_callback_ = model_load_callback;
  if (state() != NOT_RUNNING) {
    model_load_callback.Run(type(),
                            SyncError(FROM_HERE, SyncError::DATATYPE_ERROR,
                                      "Model already running", type()));
    return;
  }

  state_ = MODEL_STARTING;
  // Since we can't be called multiple times before Stop() is called,
  // |shared_change_processor_| must be NULL here.
  DCHECK(!shared_change_processor_.get());
  shared_change_processor_ = CreateSharedChangeProcessor();
  DCHECK(shared_change_processor_.get());
  if (!StartModels()) {
    // If we are waiting for some external service to load before associating
    // or we failed to start the models, we exit early.
    DCHECK(state() == MODEL_STARTING || state() == NOT_RUNNING);
    return;
  }

  OnModelLoaded();
}

void NonUIDataTypeController::OnModelLoaded() {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(state_, MODEL_STARTING);
  state_ = MODEL_LOADED;
  model_load_callback_.Run(type(), SyncError());
}

bool NonUIDataTypeController::StartModels() {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(state_, MODEL_STARTING);
  // By default, no additional services need to be started before we can proceed
  // with model association.
  return true;
}

void NonUIDataTypeController::StopModels() {
  DCHECK(CalledOnValidThread());
}

void NonUIDataTypeController::StartAssociating(
    const StartCallback& start_callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!start_callback.is_null());
  DCHECK_EQ(state_, MODEL_LOADED);
  state_ = ASSOCIATING;

  // Store UserShare now while on UI thread to avoid potential race
  // condition in StartAssociationWithSharedChangeProcessor.
  DCHECK(sync_client_->GetSyncService());
  user_share_ = sync_client_->GetSyncService()->GetUserShare();

  start_callback_ = start_callback;
  if (!StartAssociationAsync()) {
    SyncError error(FROM_HERE, SyncError::DATATYPE_ERROR,
                    "Failed to post StartAssociation", type());
    SyncMergeResult local_merge_result(type());
    local_merge_result.set_error(error);
    StartDone(ASSOCIATION_FAILED, local_merge_result, SyncMergeResult(type()));
    // StartDone should have cleared the SharedChangeProcessor.
    DCHECK(!shared_change_processor_.get());
    return;
  }
}

void NonUIDataTypeController::Stop() {
  DCHECK(CalledOnValidThread());

  if (state() == NOT_RUNNING)
    return;

  // Disconnect the change processor. At this point, the
  // SyncableService can no longer interact with the Syncer, even if
  // it hasn't finished MergeDataAndStartSyncing.
  DisconnectSharedChangeProcessor();

  // If we haven't finished starting, we need to abort the start.
  bool service_started = state() == ASSOCIATING || state() == RUNNING;
  state_ = service_started ? STOPPING : NOT_RUNNING;
  StopModels();

  if (service_started)
    StopSyncableService();

  shared_change_processor_ = nullptr;
  state_ = NOT_RUNNING;
}

std::string NonUIDataTypeController::name() const {
  // For logging only.
  return ModelTypeToString(type());
}

DataTypeController::State NonUIDataTypeController::state() const {
  return state_;
}

NonUIDataTypeController::NonUIDataTypeController()
    : DirectoryDataTypeController(UNSPECIFIED, base::Closure(), nullptr) {}

NonUIDataTypeController::~NonUIDataTypeController() {}

void NonUIDataTypeController::StartDone(
    DataTypeController::ConfigureResult start_result,
    const SyncMergeResult& local_merge_result,
    const SyncMergeResult& syncer_merge_result) {
  DCHECK(CalledOnValidThread());

  DataTypeController::State new_state;
  if (IsSuccessfulResult(start_result)) {
    new_state = RUNNING;
  } else {
    new_state = (start_result == ASSOCIATION_FAILED ? DISABLED : NOT_RUNNING);
  }

  // If we failed to start up, and we haven't been stopped yet, we need to
  // ensure we clean up the local service and shared change processor properly.
  if (new_state != RUNNING && state() != NOT_RUNNING && state() != STOPPING) {
    DisconnectSharedChangeProcessor();
    StopSyncableService();
    shared_change_processor_ = nullptr;
  }

  // It's possible to have StartDone called first from the UI thread
  // (due to Stop being called) and then posted from the non-UI thread. In
  // this case, we drop the second call because we've already been stopped.
  if (state_ == NOT_RUNNING) {
    return;
  }

  state_ = new_state;
  if (state_ != RUNNING) {
    // Start failed.
    StopModels();
    RecordStartFailure(start_result);
  }

  start_callback_.Run(start_result, local_merge_result, syncer_merge_result);
}

void NonUIDataTypeController::RecordStartFailure(ConfigureResult result) {
  DCHECK(CalledOnValidThread());
  UMA_HISTOGRAM_ENUMERATION("Sync.DataTypeStartFailures",
                            ModelTypeToHistogramInt(type()), MODEL_TYPE_COUNT);
#define PER_DATA_TYPE_MACRO(type_str)                                    \
  UMA_HISTOGRAM_ENUMERATION("Sync." type_str "ConfigureFailure", result, \
                            MAX_CONFIGURE_RESULT);
  SYNC_DATA_TYPE_HISTOGRAM(type());
#undef PER_DATA_TYPE_MACRO
}

void NonUIDataTypeController::DisableImpl(const SyncError& error) {
  DCHECK(CalledOnValidThread());
  if (!model_load_callback_.is_null()) {
    model_load_callback_.Run(type(), error);
  }
}

bool NonUIDataTypeController::StartAssociationAsync() {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(state(), ASSOCIATING);
  return PostTaskOnBackendThread(
      FROM_HERE,
      base::Bind(
          &SharedChangeProcessor::StartAssociation, shared_change_processor_,
          BindToCurrentThread(base::Bind(&NonUIDataTypeController::StartDone,
                                         base::AsWeakPtr(this))),
          sync_client_, user_share_, base::Passed(CreateErrorHandler())));
}

ChangeProcessor* NonUIDataTypeController::GetChangeProcessor() const {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(state_, RUNNING);
  return shared_change_processor_->generic_change_processor();
}

void NonUIDataTypeController::DisconnectSharedChangeProcessor() {
  DCHECK(CalledOnValidThread());
  // |shared_change_processor_| can already be NULL if Stop() is
  // called after StartDone(_, DISABLED, _).
  if (shared_change_processor_.get()) {
    shared_change_processor_->Disconnect();
  }
}

void NonUIDataTypeController::StopSyncableService() {
  DCHECK(CalledOnValidThread());
  if (shared_change_processor_.get()) {
    PostTaskOnBackendThread(FROM_HERE,
                            base::Bind(&SharedChangeProcessor::StopLocalService,
                                       shared_change_processor_));
  }
}

std::unique_ptr<DataTypeErrorHandler>
NonUIDataTypeController::CreateErrorHandler() {
  DCHECK(CalledOnValidThread());
  return base::MakeUnique<DataTypeErrorHandlerImpl>(
      base::ThreadTaskRunnerHandle::Get(), dump_stack_,
      base::Bind(&NonUIDataTypeController::DisableImpl, base::AsWeakPtr(this)));
}

}  // namespace syncer
