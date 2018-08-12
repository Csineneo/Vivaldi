// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BACKGROUND_SYNC_BACKGROUND_SYNC_SERVICE_IMPL_H_
#define CONTENT_BROWSER_BACKGROUND_SYNC_BACKGROUND_SYNC_SERVICE_IMPL_H_

#include <stdint.h>

#include "base/id_map.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_vector.h"
#include "content/browser/background_sync/background_sync_manager.h"
#include "content/common/background_sync_service.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace content {

class BackgroundSyncContextImpl;

class CONTENT_EXPORT BackgroundSyncServiceImpl
    : public NON_EXPORTED_BASE(mojom::BackgroundSyncService) {
 public:
  BackgroundSyncServiceImpl(
      BackgroundSyncContextImpl* background_sync_context,
      mojo::InterfaceRequest<mojom::BackgroundSyncService> request);

  ~BackgroundSyncServiceImpl() override;

 private:
  friend class BackgroundSyncServiceImplTest;

  // mojom::BackgroundSyncService methods:
  void Register(content::mojom::SyncRegistrationPtr options,
                int64_t sw_registration_id,
                const RegisterCallback& callback) override;
  void GetRegistrations(int64_t sw_registration_id,
                        const GetRegistrationsCallback& callback) override;

  void OnRegisterResult(const RegisterCallback& callback,
                        BackgroundSyncStatus status,
                        scoped_ptr<BackgroundSyncRegistration> result);
  void OnGetRegistrationsResult(
      const GetRegistrationsCallback& callback,
      BackgroundSyncStatus status,
      scoped_ptr<ScopedVector<BackgroundSyncRegistration>> result);

  // Called when an error is detected on binding_.
  void OnConnectionError();

  // background_sync_context_ owns this.
  BackgroundSyncContextImpl* background_sync_context_;

  mojo::Binding<mojom::BackgroundSyncService> binding_;

  base::WeakPtrFactory<BackgroundSyncServiceImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundSyncServiceImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_BACKGROUND_SYNC_BACKGROUND_SYNC_SERVICE_IMPL_H_
