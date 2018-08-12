// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/internal_components_factory_impl.h"

#include <string>
#include <vector>

#include "sync/engine/backoff_delay_provider.h"
#include "sync/engine/syncer.h"
#include "sync/engine/sync_scheduler_impl.h"
#include "sync/sessions/sync_session_context.h"
#include "sync/syncable/on_disk_directory_backing_store.h"

using base::TimeDelta;

namespace syncer {

InternalComponentsFactoryImpl::InternalComponentsFactoryImpl(
    const Switches& switches) : switches_(switches) {
}

InternalComponentsFactoryImpl::~InternalComponentsFactoryImpl() { }

std::unique_ptr<SyncScheduler> InternalComponentsFactoryImpl::BuildScheduler(
    const std::string& name,
    sessions::SyncSessionContext* context,
    CancelationSignal* cancelation_signal) {
  std::unique_ptr<BackoffDelayProvider> delay(
      BackoffDelayProvider::FromDefaults());

  if (switches_.backoff_override == BACKOFF_SHORT_INITIAL_RETRY_OVERRIDE)
    delay.reset(BackoffDelayProvider::WithShortInitialRetryOverride());

  return std::unique_ptr<SyncScheduler>(new SyncSchedulerImpl(
      name, delay.release(), context, new Syncer(cancelation_signal)));
}

std::unique_ptr<sessions::SyncSessionContext>
InternalComponentsFactoryImpl::BuildContext(
    ServerConnectionManager* connection_manager,
    syncable::Directory* directory,
    ExtensionsActivity* extensions_activity,
    const std::vector<SyncEngineEventListener*>& listeners,
    sessions::DebugInfoGetter* debug_info_getter,
    ModelTypeRegistry* model_type_registry,
    const std::string& invalidation_client_id) {
  return std::unique_ptr<sessions::SyncSessionContext>(
      new sessions::SyncSessionContext(
          connection_manager, directory, extensions_activity, listeners,
          debug_info_getter, model_type_registry,
          switches_.encryption_method == ENCRYPTION_KEYSTORE,
          switches_.pre_commit_updates_policy ==
              FORCE_ENABLE_PRE_COMMIT_UPDATE_AVOIDANCE,
          invalidation_client_id));
}

std::unique_ptr<syncable::DirectoryBackingStore>
InternalComponentsFactoryImpl::BuildDirectoryBackingStore(
    StorageOption storage,
    const std::string& dir_name,
    const base::FilePath& backing_filepath) {
  if (storage == STORAGE_ON_DISK) {
    return std::unique_ptr<syncable::DirectoryBackingStore>(
        new syncable::OnDiskDirectoryBackingStore(dir_name, backing_filepath));
  } else {
    NOTREACHED();
    return std::unique_ptr<syncable::DirectoryBackingStore>();
  }
}

InternalComponentsFactory::Switches
InternalComponentsFactoryImpl::GetSwitches() const {
  return switches_;
}

}  // namespace syncer
