// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/utility/shell_content_utility_client.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "content/public/common/service_registry.h"
#include "content/public/test/test_mojo_app.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace content {

namespace {

class TestMojoServiceImpl : public mojom::TestMojoService {
 public:
  static void Create(mojo::InterfaceRequest<mojom::TestMojoService> request) {
    new TestMojoServiceImpl(std::move(request));
  }

  // mojom::TestMojoService implementation:
  void DoSomething(const DoSomethingCallback& callback) override {
    callback.Run();
  }

  void GetRequestorName(const GetRequestorNameCallback& callback) override {
    NOTREACHED();
  }

 private:
  explicit TestMojoServiceImpl(
      mojo::InterfaceRequest<mojom::TestMojoService> request)
      : binding_(this, std::move(request)) {}

  mojo::StrongBinding<mojom::TestMojoService> binding_;

  DISALLOW_COPY_AND_ASSIGN(TestMojoServiceImpl);
};

std::unique_ptr<shell::ShellClient> CreateTestApp(
    const base::Closure& quit_closure) {
  return std::unique_ptr<shell::ShellClient>(new TestMojoApp);
}

}  // namespace

ShellContentUtilityClient::~ShellContentUtilityClient() {
}

void ShellContentUtilityClient::RegisterMojoApplications(
    StaticMojoApplicationMap* apps) {
  MojoApplicationInfo app_info;
  app_info.application_factory = base::Bind(&CreateTestApp);
  apps->insert(std::make_pair(kTestMojoAppUrl, app_info));
}

void ShellContentUtilityClient::RegisterMojoServices(
    ServiceRegistry* registry) {
  registry->AddService(base::Bind(&TestMojoServiceImpl::Create));
}

}  // namespace content
