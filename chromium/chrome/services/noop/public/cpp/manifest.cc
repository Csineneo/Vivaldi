// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/noop/public/cpp/manifest.h"

#include "base/no_destructor.h"
#include "chrome/services/noop/public/mojom/noop.mojom.h"
#include "services/service_manager/public/cpp/manifest_builder.h"

const service_manager::Manifest& GetNoopManifest() {
  static base::NoDestructor<service_manager::Manifest> manifest{
      service_manager::ManifestBuilder()
          .WithServiceName(chrome::mojom::kNoopServiceName)
          .WithDisplayName("No-op Service")
          .WithOptions(service_manager::ManifestOptionsBuilder()
                           .WithSandboxType("network")
                           .WithInstanceSharingPolicy(
                               service_manager::Manifest::
                                   InstanceSharingPolicy::kSharedAcrossGroups)
                           .Build())
          .ExposeCapability(
              "noop",
              service_manager::Manifest::InterfaceList<chrome::mojom::Noop>())

          .Build()};
  return *manifest;
}
