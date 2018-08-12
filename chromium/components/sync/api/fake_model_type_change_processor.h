// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_API_FAKE_MODEL_TYPE_CHANGE_PROCESSOR_H_
#define COMPONENTS_SYNC_API_FAKE_MODEL_TYPE_CHANGE_PROCESSOR_H_

#include <memory>
#include <string>

#include "components/sync/api/metadata_change_list.h"
#include "components/sync/api/model_type_change_processor.h"
#include "components/sync/base/model_type.h"

namespace syncer {

class ModelTypeService;

// A ModelTypeChangeProcessor implementation for tests.
class FakeModelTypeChangeProcessor : public ModelTypeChangeProcessor {
 public:
  static std::unique_ptr<ModelTypeChangeProcessor> Create(
      ModelType type,
      ModelTypeService* service);

  FakeModelTypeChangeProcessor();
  ~FakeModelTypeChangeProcessor() override;

  // ModelTypeChangeProcessor overrides
  void Put(const std::string& client_tag,
           std::unique_ptr<EntityData> entity_data,
           MetadataChangeList* metadata_change_list) override;
  void Delete(const std::string& client_tag,
              MetadataChangeList* metadata_change_list) override;
  void OnMetadataLoaded(SyncError error,
                        std::unique_ptr<MetadataBatch> batch) override;
  void OnSyncStarting(std::unique_ptr<DataTypeErrorHandler> error_handler,
                      const StartCallback& callback) override;
  void DisableSync() override;
  SyncError CreateAndUploadError(const tracked_objects::Location& location,
                                 const std::string& message) override;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_API_FAKE_MODEL_TYPE_CHANGE_PROCESSOR_H_
