// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/test_service/test_request_tracker_application.h"

#include <assert.h>
#include <utility>

#include "mojo/public/c/system/main.h"
#include "mojo/services/test_service/test_time_service_impl.h"
#include "mojo/shell/public/cpp/application_runner.h"
#include "mojo/shell/public/cpp/connection.h"

namespace mojo {
namespace test {

TestRequestTrackerApplication::TestRequestTrackerApplication() {}
TestRequestTrackerApplication::~TestRequestTrackerApplication() {}

void TestRequestTrackerApplication::Initialize(Connector* connector,
                                               const std::string& url,
                                               uint32_t id,
                                               uint32_t user_id) {
  connector_ = connector;
}

bool TestRequestTrackerApplication::AcceptConnection(Connection* connection) {
  // Every instance of the service and recorder shares the context.
  // Note, this app is single-threaded, so this is thread safe.
  connection->AddInterface<TestTimeService>(this);
  connection->AddInterface<TestRequestTracker>(this);
  connection->AddInterface<TestTrackedRequestService>(this);
  return true;
}

void TestRequestTrackerApplication::Create(
    Connection* connection,
    InterfaceRequest<TestTimeService> request) {
  new TestTimeServiceImpl(connector_, std::move(request));
}

void TestRequestTrackerApplication::Create(
    Connection* connection,
    InterfaceRequest<TestRequestTracker> request) {
  new TestRequestTrackerImpl(std::move(request), &context_);
}

void TestRequestTrackerApplication::Create(
    Connection* connection,
    InterfaceRequest<TestTrackedRequestService> request) {
  new TestTrackedRequestServiceImpl(std::move(request), &context_);
}

}  // namespace test
}  // namespace mojo

MojoResult MojoMain(MojoHandle shell_handle) {
  mojo::ApplicationRunner runner(
      new mojo::test::TestRequestTrackerApplication);
  return runner.Run(shell_handle);
}
