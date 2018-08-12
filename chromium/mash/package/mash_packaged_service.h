// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MASH_PACKAGE_MASH_PACKAGED_SERVICE_H_
#define MASH_PACKAGE_MASH_PACKAGED_SERVICE_H_

#include <memory>

#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/shell/public/cpp/interface_factory.h"
#include "services/shell/public/cpp/service.h"
#include "services/shell/public/interfaces/service_factory.mojom.h"

namespace mash {

// MashPackagedService is a Service implementation that starts all the mash
// apps. It's used when mash is packaged inside chrome or tests. To use you'll
// need a manifest similar to what is used by chrome and browser_tests.
// Things to do when adding a new service/app:
//   - Add a manifest to the new service.
//   - Update manifests of services that are going to use this new service. e.g.
//     chrome_manifest.
//   - Add the new serivce to be a data_dep of the service that is using this
//     new service.
//   - Add the new service to chrome_mash's deps section and packaged_services
//     section.
//   - Add the new service to mash_browser_tests's deps section and
//     packaged_services section.
//   - Add an entry for the new service in MashPackagedService::CreateService().
class MashPackagedService
    : public shell::Service,
      public shell::mojom::ServiceFactory,
      public shell::InterfaceFactory<shell::mojom::ServiceFactory> {
 public:
  MashPackagedService();
  ~MashPackagedService() override;

  // shell::Service:
  bool OnConnect(const shell::Identity& remote_identity,
                 shell::InterfaceRegistry* registry) override;

  // shell::InterfaceFactory<ServiceFactory>
  void Create(const shell::Identity& remote_identity,
              mojo::InterfaceRequest<ServiceFactory> request) override;

  // ServiceFactory:
  void CreateService(shell::mojom::ServiceRequest request,
                     const std::string& mojo_name) override;

 private:
  std::unique_ptr<shell::Service> CreateService(const std::string& name);

  mojo::BindingSet<ServiceFactory> service_factory_bindings_;
  std::unique_ptr<shell::Service> service_;

  DISALLOW_COPY_AND_ASSIGN(MashPackagedService);
};

}  // namespace mash

#endif  // MASH_PACKAGE_MASH_PACKAGED_SERVICE_H_
