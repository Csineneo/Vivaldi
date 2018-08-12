// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/driver/model_type_controller.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/sync/api/data_type_error_handler_impl.h"
#include "components/sync/api/model_type_change_processor.h"
#include "components/sync/api/model_type_service.h"
#include "components/sync/api/sync_error.h"
#include "components/sync/api/sync_merge_result.h"
#include "components/sync/base/bind_to_task_runner.h"
#include "components/sync/base/data_type_histogram.h"
#include "components/sync/core/activation_context.h"
#include "components/sync/driver/backend_data_type_configurer.h"
#include "components/sync/driver/sync_client.h"

namespace syncer {

ModelTypeController::ModelTypeController(
    ModelType type,
    const base::Closure& dump_stack,
    SyncClient* sync_client,
    const scoped_refptr<base::SingleThreadTaskRunner>& model_thread)
    : DataTypeController(type, dump_stack),
      sync_client_(sync_client),
      model_thread_(model_thread),
      sync_prefs_(sync_client->GetPrefService()),
      state_(NOT_RUNNING) {
  DCHECK(model_thread_);
}

ModelTypeController::~ModelTypeController() {}

bool ModelTypeController::ShouldLoadModelBeforeConfigure() const {
  // USS datatypes require loading models because model controls storage where
  // data type context and progress marker are persisted.
  return true;
}

void ModelTypeController::LoadModels(
    const ModelLoadCallback& model_load_callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!model_load_callback.is_null());
  model_load_callback_ = model_load_callback;

  if (state() != NOT_RUNNING) {
    LoadModelsDone(RUNTIME_ERROR,
                   SyncError(FROM_HERE, SyncError::DATATYPE_ERROR,
                             "Model already running", type()));
    return;
  }

  state_ = MODEL_STARTING;

  // Callback that posts back to the UI thread.
  ModelTypeChangeProcessor::StartCallback callback =
      BindToCurrentThread(base::Bind(&ModelTypeController::OnProcessorStarted,
                                     base::AsWeakPtr(this)));

  // Start the type processor on the model thread.
  model_thread_->PostTask(
      FROM_HERE, base::Bind(&ModelTypeService::OnSyncStarting,
                            sync_client_->GetModelTypeServiceForType(type()),
                            base::Passed(CreateErrorHandler()), callback));
}

void ModelTypeController::GetAllNodes(const AllNodesCallback& callback) {
  base::WeakPtr<ModelTypeService> service =
      sync_client_->GetModelTypeServiceForType(type());
  // TODO(gangwu): Casting should happen "near" where the processor factory has
  // code that instantiates a new processor.
  SharedModelTypeProcessor* processor =
      static_cast<SharedModelTypeProcessor*>(service->change_processor());
  model_thread_->PostTask(
      FROM_HERE, base::Bind(&SharedModelTypeProcessor::GetAllNodes,
                            base::Unretained(processor),
                            base::ThreadTaskRunnerHandle::Get(), callback));
}

void ModelTypeController::LoadModelsDone(ConfigureResult result,
                                         const SyncError& error) {
  DCHECK(CalledOnValidThread());

  if (state_ == NOT_RUNNING) {
    // The callback arrived on the UI thread after the type has been already
    // stopped.
    RecordStartFailure(ABORTED);
    return;
  }

  if (IsSuccessfulResult(result)) {
    DCHECK_EQ(MODEL_STARTING, state_);
    state_ = MODEL_LOADED;
  } else {
    RecordStartFailure(result);
  }

  if (!model_load_callback_.is_null()) {
    model_load_callback_.Run(type(), error);
  }
}

void ModelTypeController::OnProcessorStarted(
    SyncError error,
    std::unique_ptr<ActivationContext> activation_context) {
  DCHECK(CalledOnValidThread());
  // Hold on to the activation context until ActivateDataType is called.
  if (state_ == MODEL_STARTING) {
    activation_context_ = std::move(activation_context);
  }
  // TODO(stanisc): Figure out if UNRECOVERABLE_ERROR is OK in this case.
  ConfigureResult result = error.IsSet() ? UNRECOVERABLE_ERROR : OK;
  LoadModelsDone(result, error);
}

void ModelTypeController::RegisterWithBackend(
    BackendDataTypeConfigurer* configurer) {
  DCHECK(CalledOnValidThread());
  if (activated_)
    return;
  DCHECK(configurer);
  DCHECK(activation_context_);
  DCHECK_EQ(MODEL_LOADED, state_);
  configurer->ActivateNonBlockingDataType(type(),
                                          std::move(activation_context_));
  activated_ = true;
}

void ModelTypeController::StartAssociating(
    const StartCallback& start_callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!start_callback.is_null());
  DCHECK_EQ(MODEL_LOADED, state_);

  state_ = RUNNING;

  // There is no association, just call back promptly.
  SyncMergeResult merge_result(type());
  start_callback.Run(OK, merge_result, merge_result);
}

void ModelTypeController::ActivateDataType(
    BackendDataTypeConfigurer* configurer) {
  DCHECK(CalledOnValidThread());
  DCHECK(configurer);
  DCHECK_EQ(RUNNING, state_);
  // In contrast with directory datatypes, non-blocking data types should be
  // activated in RegisterWithBackend. activation_context_ should be passed
  // to backend before call to ActivateDataType.
  DCHECK(!activation_context_);
}

void ModelTypeController::DeactivateDataType(
    BackendDataTypeConfigurer* configurer) {
  DCHECK(CalledOnValidThread());
  DCHECK(configurer);
  DCHECK(activated_);
  configurer->DeactivateNonBlockingDataType(type());
  activated_ = false;
}

void ModelTypeController::Stop() {
  DCHECK(CalledOnValidThread());

  if (state() == NOT_RUNNING)
    return;

  // Check preferences if datatype is not in preferred datatypes. Only call
  // DisableSync if service is ready to handle it (controller is in loaded
  // state).
  ModelTypeSet preferred_types =
      sync_prefs_.GetPreferredDataTypes(ModelTypeSet(type()));
  if ((state() == MODEL_LOADED || state() == RUNNING) &&
      (!sync_prefs_.IsFirstSetupComplete() || !preferred_types.Has(type()))) {
    model_thread_->PostTask(
        FROM_HERE,
        base::Bind(&ModelTypeService::DisableSync,
                   sync_client_->GetModelTypeServiceForType(type())));
  }

  state_ = NOT_RUNNING;
}

std::string ModelTypeController::name() const {
  // For logging only.
  return ModelTypeToString(type());
}

DataTypeController::State ModelTypeController::state() const {
  return state_;
}

std::unique_ptr<DataTypeErrorHandler>
ModelTypeController::CreateErrorHandler() {
  DCHECK(CalledOnValidThread());
  return base::MakeUnique<DataTypeErrorHandlerImpl>(
      base::ThreadTaskRunnerHandle::Get(), dump_stack_,
      base::Bind(&ModelTypeController::ReportLoadModelError,
                 base::AsWeakPtr(this)));
}

void ModelTypeController::ReportLoadModelError(const SyncError& error) {
  DCHECK(CalledOnValidThread());
  LoadModelsDone(UNRECOVERABLE_ERROR, error);
}

void ModelTypeController::RecordStartFailure(ConfigureResult result) const {
  DCHECK(CalledOnValidThread());
  UMA_HISTOGRAM_ENUMERATION("Sync.DataTypeStartFailures",
                            ModelTypeToHistogramInt(type()), MODEL_TYPE_COUNT);
#define PER_DATA_TYPE_MACRO(type_str)                                    \
  UMA_HISTOGRAM_ENUMERATION("Sync." type_str "ConfigureFailure", result, \
                            MAX_CONFIGURE_RESULT);
  SYNC_DATA_TYPE_HISTOGRAM(type());
#undef PER_DATA_TYPE_MACRO
}

}  // namespace syncer
