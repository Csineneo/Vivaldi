// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/shared_model_type_processor.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/thread_task_runner_handle.h"
#include "sync/engine/commit_queue.h"
#include "sync/internal_api/public/activation_context.h"
#include "sync/internal_api/public/model_type_entity.h"
#include "sync/syncable/syncable_util.h"

namespace syncer_v2 {

namespace {

class ModelTypeProcessorProxy : public ModelTypeProcessor {
 public:
  ModelTypeProcessorProxy(
      const base::WeakPtr<ModelTypeProcessor>& processor,
      const scoped_refptr<base::SequencedTaskRunner>& processor_task_runner);
  ~ModelTypeProcessorProxy() override;

  void OnConnect(scoped_ptr<CommitQueue> worker) override;
  void OnCommitCompleted(const DataTypeState& type_state,
                         const CommitResponseDataList& response_list) override;
  void OnUpdateReceived(const DataTypeState& type_state,
                        const UpdateResponseDataList& response_list,
                        const UpdateResponseDataList& pending_updates) override;

 private:
  base::WeakPtr<ModelTypeProcessor> processor_;
  scoped_refptr<base::SequencedTaskRunner> processor_task_runner_;
};

ModelTypeProcessorProxy::ModelTypeProcessorProxy(
    const base::WeakPtr<ModelTypeProcessor>& processor,
    const scoped_refptr<base::SequencedTaskRunner>& processor_task_runner)
    : processor_(processor), processor_task_runner_(processor_task_runner) {}

ModelTypeProcessorProxy::~ModelTypeProcessorProxy() {}

void ModelTypeProcessorProxy::OnConnect(scoped_ptr<CommitQueue> worker) {
  processor_task_runner_->PostTask(
      FROM_HERE, base::Bind(&ModelTypeProcessor::OnConnect, processor_,
                            base::Passed(std::move(worker))));
}

void ModelTypeProcessorProxy::OnCommitCompleted(
    const DataTypeState& type_state,
    const CommitResponseDataList& response_list) {
  processor_task_runner_->PostTask(
      FROM_HERE, base::Bind(&ModelTypeProcessor::OnCommitCompleted, processor_,
                            type_state, response_list));
}

void ModelTypeProcessorProxy::OnUpdateReceived(
    const DataTypeState& type_state,
    const UpdateResponseDataList& response_list,
    const UpdateResponseDataList& pending_updates) {
  processor_task_runner_->PostTask(
      FROM_HERE, base::Bind(&ModelTypeProcessor::OnUpdateReceived, processor_,
                            type_state, response_list, pending_updates));
}

}  // namespace

SharedModelTypeProcessor::SharedModelTypeProcessor(syncer::ModelType type,
                                                   ModelTypeService* service)
    : type_(type),
      is_enabled_(false),
      is_connected_(false),
      service_(service),
      weak_ptr_factory_for_ui_(this),
      weak_ptr_factory_for_sync_(this) {
  DCHECK(service);
}

SharedModelTypeProcessor::~SharedModelTypeProcessor() {}

void SharedModelTypeProcessor::Start(StartCallback callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "Starting " << ModelTypeToString(type_);

  is_enabled_ = true;

  // TODO(stanisc): At some point, this should be loaded from storage.
  data_type_state_.progress_marker.set_data_type_id(
      GetSpecificsFieldNumberFromModelType(type_));

  scoped_ptr<ActivationContext> activation_context =
      make_scoped_ptr(new ActivationContext);
  activation_context->data_type_state = data_type_state_;
  activation_context->saved_pending_updates = GetPendingUpdates();
  activation_context->type_processor = make_scoped_ptr(
      new ModelTypeProcessorProxy(weak_ptr_factory_for_sync_.GetWeakPtr(),
                                  base::ThreadTaskRunnerHandle::Get()));

  callback.Run(syncer::SyncError(), std::move(activation_context));
}

bool SharedModelTypeProcessor::IsEnabled() const {
  DCHECK(CalledOnValidThread());
  return is_enabled_;
}

bool SharedModelTypeProcessor::IsConnected() const {
  DCHECK(CalledOnValidThread());
  return is_connected_;
}

// TODO(stanisc): crbug.com/537027: This needs to be called from
// DataTypeController when the type is disabled
void SharedModelTypeProcessor::Disable() {
  DCHECK(CalledOnValidThread());
  is_enabled_ = false;
  Stop();
  ClearSyncState();
}

void SharedModelTypeProcessor::Stop() {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "Stopping " << ModelTypeToString(type_);
  is_connected_ = false;
  weak_ptr_factory_for_sync_.InvalidateWeakPtrs();
  worker_.reset();

  ClearTransientSyncState();
}

base::WeakPtr<SharedModelTypeProcessor>
SharedModelTypeProcessor::AsWeakPtrForUI() {
  DCHECK(CalledOnValidThread());
  return weak_ptr_factory_for_ui_.GetWeakPtr();
}

void SharedModelTypeProcessor::OnConnect(scoped_ptr<CommitQueue> worker) {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "Successfully connected " << ModelTypeToString(type_);

  is_connected_ = true;
  worker_ = std::move(worker);

  FlushPendingCommitRequests();
}

void SharedModelTypeProcessor::Put(const std::string& client_tag,
                                   scoped_ptr<EntityData> entity_data,
                                   MetadataChangeList* metadata_change_list) {
  DCHECK(entity_data.get());
  DCHECK(!entity_data->is_deleted());
  DCHECK(!entity_data->non_unique_name.empty());
  DCHECK_EQ(type_, syncer::GetModelTypeFromSpecifics(entity_data->specifics));

  // If the service specified an overriding hash, use that, otherwise generate
  // one from the tag.
  // TODO(skym): This behavior should be delayed, once crbug.com/561818 is fixed
  // we will only perform this logic in the create case.
  const std::string client_tag_hash(
      entity_data->client_tag_hash.empty()
          ? syncer::syncable::GenerateSyncableHash(type_, client_tag)
          : entity_data->client_tag_hash);

  base::Time now = base::Time::Now();

  ModelTypeEntity* entity = nullptr;
  // TODO(stanisc): crbug.com/561818: Search by client_tag rather than
  // client_tag_hash.
  auto it = entities_.find(client_tag_hash);
  if (it == entities_.end()) {
    // The service is creating a new entity.
    scoped_ptr<ModelTypeEntity> scoped_entity = ModelTypeEntity::CreateNew(
        client_tag, client_tag_hash, entity_data->id, now);
    entity = scoped_entity.get();
    entities_.insert(
        std::make_pair(client_tag_hash, std::move(scoped_entity)));
  } else {
    // The service is updating an existing entity.
    entity = it->second.get();
    DCHECK_EQ(client_tag, entity->client_key());
  }

  // TODO(stanisc): crbug.com/561829: Avoid committing a change if there is no
  // actual change.
  entity->MakeLocalChange(std::move(entity_data), now);
  metadata_change_list->UpdateMetadata(client_tag, entity->metadata());

  FlushPendingCommitRequests();
}

void SharedModelTypeProcessor::Delete(
    const std::string& client_key,
    MetadataChangeList* metadata_change_list) {
  const std::string client_tag_hash(
      syncer::syncable::GenerateSyncableHash(type_, client_key));

  // TODO(skym): crbug.com/561818: Search by client_tag rather than
  // client_tag_hash.
  auto it = entities_.find(client_tag_hash);
  if (it == entities_.end()) {
    // That's unusual, but not necessarily a bad thing.
    // Missing is as good as deleted as far as the model is concerned.
    DLOG(WARNING) << "Attempted to delete missing item."
                  << " client tag: " << client_key;
    return;
  }

  ModelTypeEntity* entity = it->second.get();
  entity->Delete();

  metadata_change_list->UpdateMetadata(client_key, entity->metadata());

  FlushPendingCommitRequests();
}

void SharedModelTypeProcessor::FlushPendingCommitRequests() {
  CommitRequestDataList commit_requests;

  // Don't bother sending anything if there's no one to send to.
  if (!IsConnected())
    return;

  // Don't send anything if the type is not ready to handle commits.
  if (!data_type_state_.initial_sync_done)
    return;

  // TODO(rlarocque): Do something smarter than iterate here.
  for (auto it = entities_.begin(); it != entities_.end(); ++it) {
    if (it->second->RequiresCommitRequest()) {
      CommitRequestData request;
      it->second->InitializeCommitRequestData(&request);
      commit_requests.push_back(request);
      it->second->SetCommitRequestInProgress();
    }
  }

  if (!commit_requests.empty())
    worker_->EnqueueForCommit(commit_requests);
}

void SharedModelTypeProcessor::OnCommitCompleted(
    const DataTypeState& type_state,
    const CommitResponseDataList& response_list) {
  scoped_ptr<MetadataChangeList> change_list =
      service_->CreateMetadataChangeList();

  data_type_state_ = type_state;
  change_list->UpdateDataTypeState(data_type_state_);

  for (auto list_it = response_list.begin(); list_it != response_list.end();
       ++list_it) {
    const CommitResponseData& response_data = *list_it;
    const std::string& client_tag_hash = response_data.client_tag_hash;

    auto it = entities_.find(client_tag_hash);
    if (it == entities_.end()) {
      NOTREACHED() << "Received commit response for missing item."
                   << " type: " << type_ << " client_tag: " << client_tag_hash;
      return;
    } else {
      it->second->ReceiveCommitResponse(
          response_data.id, response_data.sequence_number,
          response_data.response_version, data_type_state_.encryption_key_name);
      // TODO(stanisc): crbug.com/573333: Delete case.
      // This might be the right place to clear a metadata entry that has
      // been deleted locally and confirmed deleted by the server.
      change_list->UpdateMetadata(it->second->client_key(),
                                  it->second->metadata());
    }
  }

  // TODO(stanisc): What is the right method to submit metadata changes to the
  // service? Is using empty EntityChangeList OK?
  // TODO(stanisc): crbug.com/570085: Error handling.
  service_->ApplySyncChanges(std::move(change_list), EntityChangeList());
}

void SharedModelTypeProcessor::OnUpdateReceived(
    const DataTypeState& data_type_state,
    const UpdateResponseDataList& response_list,
    const UpdateResponseDataList& pending_updates) {
  scoped_ptr<MetadataChangeList> metadata_changes =
      service_->CreateMetadataChangeList();
  EntityChangeList entity_changes;

  metadata_changes->UpdateDataTypeState(data_type_state);
  bool got_new_encryption_requirements = data_type_state_.encryption_key_name !=
                                         data_type_state.encryption_key_name;
  data_type_state_ = data_type_state;

  for (auto list_it = response_list.begin(); list_it != response_list.end();
       ++list_it) {
    const UpdateResponseData& response_data = *list_it;
    const EntityData& data = response_data.entity.value();
    const std::string& client_tag_hash = data.client_tag_hash;

    // If we're being asked to apply an update to this entity, this overrides
    // the previous pending updates.
    pending_updates_map_.erase(client_tag_hash);

    ModelTypeEntity* entity = nullptr;
    auto it = entities_.find(client_tag_hash);
    if (it == entities_.end()) {
      if (data.is_deleted()) {
        DLOG(WARNING) << "Received remote delete for a non-existing item."
                      << " client_tag_hash: " << client_tag_hash;
        continue;
      }

      // Let the service define |client_tag| based on the entity data.
      std::string client_key = service_->GetClientTag(data);

      scoped_ptr<ModelTypeEntity> scoped_entity = ModelTypeEntity::CreateNew(
          client_key, client_tag_hash, data.id, data.creation_time);
      entity = scoped_entity.get();
      entities_.insert(
          std::make_pair(client_tag_hash, std::move(scoped_entity)));

      entity_changes.push_back(
          EntityChange::CreateAdd(client_key, response_data.entity));

    } else {
      entity = it->second.get();
      if (data.is_deleted()) {
        entity_changes.push_back(
            EntityChange::CreateDelete(entity->client_key()));
      } else {
        // TODO(stanisc): crbug.com/561829: Avoid sending an update to the
        // service if there is no actual change.
        entity_changes.push_back(EntityChange::CreateUpdate(
            entity->client_key(), response_data.entity));
      }
    }

    entity->ApplyUpdateFromServer(response_data);
    // TODO(stanisc): crbug.com/573333: Delete case.
    // This might be the right place to clear metadata entry instead of
    // updating it.
    metadata_changes->UpdateMetadata(entity->client_key(), entity->metadata());

    // TODO(stanisc): crbug.com/521867: Do something special when conflicts are
    // detected.

    // If the received entity has out of date encryption, we schedule another
    // commit to fix it.
    if (data_type_state_.encryption_key_name !=
        response_data.encryption_key_name) {
      DVLOG(2) << ModelTypeToString(type_) << ": Requesting re-encrypt commit "
               << response_data.encryption_key_name << " -> "
               << data_type_state_.encryption_key_name;
      auto it2 = entities_.find(client_tag_hash);
      it2->second->UpdateDesiredEncryptionKey(
          data_type_state_.encryption_key_name);
    }
  }

  // TODO: crbug.com/529498: stop saving pending updates.
  // Save pending updates in the appropriate data structure.
  for (auto list_it = pending_updates.begin(); list_it != pending_updates.end();
       ++list_it) {
    const UpdateResponseData& update = *list_it;
    const std::string& client_tag_hash = update.entity->client_tag_hash;

    auto lookup_it = pending_updates_map_.find(client_tag_hash);
    if (lookup_it == pending_updates_map_.end()) {
      pending_updates_map_.insert(std::make_pair(
          client_tag_hash, make_scoped_ptr(new UpdateResponseData(update))));
    } else if (lookup_it->second->response_version <= update.response_version) {
      pending_updates_map_.erase(lookup_it);
      pending_updates_map_.insert(std::make_pair(
          client_tag_hash, make_scoped_ptr(new UpdateResponseData(update))));
    } else {
      // Received update is stale, do not overwrite existing.
    }
  }

  if (got_new_encryption_requirements) {
    for (auto it = entities_.begin(); it != entities_.end(); ++it) {
      it->second->UpdateDesiredEncryptionKey(
          data_type_state_.encryption_key_name);
    }
  }

  // Inform the service of the new or updated data.
  // TODO(stanisc): crbug.com/570085: Error handling.
  service_->ApplySyncChanges(std::move(metadata_changes), entity_changes);

  // We may have new reasons to commit by the time this function is done.
  FlushPendingCommitRequests();
}

UpdateResponseDataList SharedModelTypeProcessor::GetPendingUpdates() {
  UpdateResponseDataList pending_updates_list;
  for (auto it = pending_updates_map_.begin(); it != pending_updates_map_.end();
       ++it) {
    pending_updates_list.push_back(*it->second);
  }
  return pending_updates_list;
}

void SharedModelTypeProcessor::ClearTransientSyncState() {
  for (auto it = entities_.begin(); it != entities_.end(); ++it) {
    it->second->ClearTransientSyncState();
  }
}

void SharedModelTypeProcessor::ClearSyncState() {
  for (auto it = entities_.begin(); it != entities_.end(); ++it) {
    it->second->ClearSyncState();
  }
  pending_updates_map_.clear();
  data_type_state_ = DataTypeState();
  // TODO(stanisc): crbug.com/561830, crbug.com/573333: Update the service to
  // let it know that all metadata need to be cleared from the storage.
}

}  // namespace syncer_v2
