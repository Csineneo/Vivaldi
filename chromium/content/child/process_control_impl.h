// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_CHILD_PROCESS_CONTROL_IMPL_H_
#define CONTENT_CHILD_PROCESS_CONTROL_IMPL_H_

#include <map>

#include "base/macros.h"
#include "content/common/process_control.mojom.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/shell/public/interfaces/shell_client.mojom.h"

namespace mojo {
namespace shell {
class Loader;
}  // namespace shell
}  // namespace mojo

namespace content {

// Default implementation of the mojom::ProcessControl interface.
class ProcessControlImpl : public mojom::ProcessControl {
 public:
  ProcessControlImpl();
  ~ProcessControlImpl() override;

  using NameToLoaderMap = std::map<std::string, mojo::shell::Loader*>;

  // Registers Mojo loaders for names.
  virtual void RegisterLoaders(NameToLoaderMap* name_to_loader_map) = 0;

  // ProcessControl:
  void LoadApplication(
      const mojo::String& name,
      mojo::InterfaceRequest<mojo::shell::mojom::ShellClient> request,
      const LoadApplicationCallback& callback) override;

 private:
  // Called if a LoadApplication request fails.
  virtual void OnLoadFailed() {}

  bool has_registered_loaders_ = false;
  NameToLoaderMap name_to_loader_map_;

  DISALLOW_COPY_AND_ASSIGN(ProcessControlImpl);
};

}  // namespace content

#endif  // CONTENT_CHILD_PROCESS_CONTROL_IMPL_H_
