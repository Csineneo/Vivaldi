// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_DEVICE_INFO_SERVICE_H_
#define COMPONENTS_SYNC_DRIVER_DEVICE_INFO_SERVICE_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "components/sync_driver/device_info_tracker.h"
#include "components/sync_driver/local_device_info_provider.h"
#include "sync/api/model_type_service.h"
#include "sync/api/model_type_store.h"
#include "sync/internal_api/public/simple_metadata_change_list.h"

namespace syncer {
class SyncError;
}  // namespace syncer

namespace syncer_v2 {
class ModelTypeChangeProcessor;
}  // namespace syncer_v2

namespace sync_pb {
class DeviceInfoSpecifics;
}  // namespace sync_pb

namespace sync_driver_v2 {

// USS service implementation for DEVICE_INFO model type. Handles storage of
// device info and associated sync metadata, applying/merging foreign changes,
// and allows public read access.
class DeviceInfoService : public syncer_v2::ModelTypeService,
                          public sync_driver::DeviceInfoTracker {
 public:
  typedef base::Callback<void(
      const syncer_v2::ModelTypeStore::InitCallback& callback)>
      StoreFactoryFunction;

  DeviceInfoService(
      sync_driver::LocalDeviceInfoProvider* local_device_info_provider,
      const StoreFactoryFunction& callback,
      const ChangeProcessorFactory& change_processor_factory);
  ~DeviceInfoService() override;

  // ModelTypeService implementation.
  scoped_ptr<syncer_v2::MetadataChangeList> CreateMetadataChangeList() override;
  syncer::SyncError MergeSyncData(
      scoped_ptr<syncer_v2::MetadataChangeList> metadata_change_list,
      syncer_v2::EntityDataMap entity_data_map) override;
  syncer::SyncError ApplySyncChanges(
      scoped_ptr<syncer_v2::MetadataChangeList> metadata_change_list,
      syncer_v2::EntityChangeList entity_changes) override;
  void GetData(ClientTagList client_tags, DataCallback callback) override;
  void GetAllData(DataCallback callback) override;
  std::string GetClientTag(const syncer_v2::EntityData& entity_data) override;
  void OnChangeProcessorSet() override;

  // DeviceInfoTracker implementation.
  bool IsSyncing() const override;
  scoped_ptr<sync_driver::DeviceInfo> GetDeviceInfo(
      const std::string& client_id) const override;
  ScopedVector<sync_driver::DeviceInfo> GetAllDeviceInfo() const override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  int CountActiveDevices() const override;

 private:
  friend class DeviceInfoServiceTest;

  static scoped_ptr<sync_pb::DeviceInfoSpecifics> CopyToSpecifics(
      const sync_driver::DeviceInfo& info);

  // Allocate new DeviceInfo from SyncData.
  static scoped_ptr<sync_driver::DeviceInfo> CopyToModel(
      const sync_pb::DeviceInfoSpecifics& specifics);
  // Conversion as we prepare to hand data to the processor.
  static scoped_ptr<syncer_v2::EntityData> CopyToEntityData(
      const sync_pb::DeviceInfoSpecifics& specifics);

  // Store SyncData in the cache and durable storage.
  void StoreSpecifics(scoped_ptr<sync_pb::DeviceInfoSpecifics> specifics,
                      syncer_v2::ModelTypeStore::WriteBatch* batch);
  // Delete SyncData from the cache and durable storage, returns true if there
  // was actually anything at the given tag.
  bool DeleteSpecifics(const std::string& tag,
                       syncer_v2::ModelTypeStore::WriteBatch* batch);

  // Notify all registered observers.
  void NotifyObservers();

  // Used as callback given to LocalDeviceInfoProvider.
  void OnProviderInitialized();

  // Methods used as callbacks given to DataTypeStore.
  void OnStoreCreated(syncer_v2::ModelTypeStore::Result result,
                      scoped_ptr<syncer_v2::ModelTypeStore> store);
  void OnReadAllData(
      syncer_v2::ModelTypeStore::Result result,
      scoped_ptr<syncer_v2::ModelTypeStore::RecordList> record_list);
  void OnReadAllMetadata(
      syncer_v2::ModelTypeStore::Result result,
      scoped_ptr<syncer_v2::ModelTypeStore::RecordList> metadata_records,
      const std::string& global_metadata);
  void OnCommit(syncer_v2::ModelTypeStore::Result result);

  // Checks if conditions have been met to perform reconciliation between the
  // locally provide device info and the stored device info data. If conditions
  // are met and the sets of data differ, than we condier this a local change
  // and we send it to the processor.
  void TryReconcileLocalAndStored();

  // Writes the given device info to both local storage and to sync.
  void PutAndStore(const sync_driver::DeviceInfo& device_info);

  // Persists the changes in the given aggregators and notifies observers if
  // indicated to do as such.
  void CommitAndNotify(
      scoped_ptr<syncer_v2::ModelTypeStore::WriteBatch> batch,
      scoped_ptr<syncer_v2::MetadataChangeList> metadata_change_list,
      bool should_notify);

  // |local_device_info_provider_| isn't owned.
  const sync_driver::LocalDeviceInfoProvider* const local_device_info_provider_;

  // Cache of all syncable and local data, stored by device cache guid.
  using ClientIdToSpecifics =
      std::map<std::string, scoped_ptr<sync_pb::DeviceInfoSpecifics>>;
  ClientIdToSpecifics all_data_;

  // Registered observers, not owned.
  base::ObserverList<Observer, true> observers_;

  // Used to listen for provider initialization. If the provider is already
  // initialized during our constructor then the subscription is never used.
  scoped_ptr<sync_driver::LocalDeviceInfoProvider::Subscription> subscription_;

  // In charge of actually persiting changes to disk, or loading previous data.
  scoped_ptr<syncer_v2::ModelTypeStore> store_;

  // If |local_device_info_provider_| has initialized.
  bool has_provider_initialized_ = false;
  // if |change_processor()| has been given metadata.
  bool has_metadata_loaded_ = false;

  // Should always be last member.
  base::WeakPtrFactory<DeviceInfoService> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceInfoService);
};

}  // namespace sync_driver_v2

#endif  // COMPONENTS_SYNC_DRIVER_DEVICE_INFO_SERVICE_H_
