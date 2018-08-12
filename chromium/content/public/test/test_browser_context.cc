// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/test_browser_context.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/test/null_task_runner.h"
#include "content/public/browser/permission_manager.h"
#include "content/public/test/mock_resource_context.h"
#include "content/test/mock_background_sync_controller.h"
#include "content/test/mock_ssl_host_state_delegate.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_test_util.h"
#include "storage/browser/quota/special_storage_policy.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestContextURLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  TestContextURLRequestContextGetter()
      : null_task_runner_(new base::NullTaskRunner) {
  }

  net::URLRequestContext* GetURLRequestContext() override { return &context_; }

  scoped_refptr<base::SingleThreadTaskRunner> GetNetworkTaskRunner()
      const override {
    return null_task_runner_;
  }

 private:
  ~TestContextURLRequestContextGetter() override {}

  net::TestURLRequestContext context_;
  scoped_refptr<base::SingleThreadTaskRunner> null_task_runner_;
};

}  // namespace

namespace content {

TestBrowserContext::TestBrowserContext() {
  EXPECT_TRUE(browser_context_dir_.CreateUniqueTempDir());
  BrowserContext::Initialize(this, browser_context_dir_.path());
}

TestBrowserContext::~TestBrowserContext() {
}

base::FilePath TestBrowserContext::TakePath() {
  return browser_context_dir_.Take();
}

void TestBrowserContext::SetSpecialStoragePolicy(
    storage::SpecialStoragePolicy* policy) {
  special_storage_policy_ = policy;
}

void TestBrowserContext::SetPermissionManager(
    scoped_ptr<PermissionManager> permission_manager) {
  permission_manager_ = std::move(permission_manager);
}

base::FilePath TestBrowserContext::GetPath() const {
  return browser_context_dir_.path();
}

scoped_ptr<ZoomLevelDelegate> TestBrowserContext::CreateZoomLevelDelegate(
    const base::FilePath& partition_path) {
  return scoped_ptr<ZoomLevelDelegate>();
}

bool TestBrowserContext::IsOffTheRecord() const {
  return false;
}

DownloadManagerDelegate* TestBrowserContext::GetDownloadManagerDelegate() {
  return NULL;
}

net::URLRequestContextGetter* TestBrowserContext::GetRequestContext() {
  if (!request_context_.get()) {
    request_context_ = new TestContextURLRequestContextGetter();
  }
  return request_context_.get();
}

net::URLRequestContextGetter* TestBrowserContext::GetMediaRequestContext() {
  return NULL;
}

net::URLRequestContextGetter*
TestBrowserContext::GetMediaRequestContextForRenderProcess(
    int renderer_child_id) {
  return NULL;
}

net::URLRequestContextGetter*
TestBrowserContext::GetMediaRequestContextForStoragePartition(
    const base::FilePath& partition_path,
    bool in_memory) {
  return NULL;
}

ResourceContext* TestBrowserContext::GetResourceContext() {
  if (!resource_context_)
    resource_context_.reset(new MockResourceContext(
        GetRequestContext()->GetURLRequestContext()));
  return resource_context_.get();
}

BrowserPluginGuestManager* TestBrowserContext::GetGuestManager() {
  return NULL;
}

storage::SpecialStoragePolicy* TestBrowserContext::GetSpecialStoragePolicy() {
  return special_storage_policy_.get();
}

PushMessagingService* TestBrowserContext::GetPushMessagingService() {
  return NULL;
}

SSLHostStateDelegate* TestBrowserContext::GetSSLHostStateDelegate() {
  if (!ssl_host_state_delegate_)
    ssl_host_state_delegate_.reset(new MockSSLHostStateDelegate());
  return ssl_host_state_delegate_.get();
}

PermissionManager* TestBrowserContext::GetPermissionManager() {
  return permission_manager_.get();
}

BackgroundSyncController* TestBrowserContext::GetBackgroundSyncController() {
  if (!background_sync_controller_)
    background_sync_controller_.reset(new MockBackgroundSyncController());

  return background_sync_controller_.get();
}

net::URLRequestContextGetter* TestBrowserContext::CreateRequestContext(
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors) {
  return GetRequestContext();
}

net::URLRequestContextGetter*
TestBrowserContext::CreateRequestContextForStoragePartition(
    const base::FilePath& partition_path,
    bool in_memory,
    ProtocolHandlerMap* protocol_handlers,
    URLRequestInterceptorScopedVector request_interceptors) {
  return nullptr;
}

}  // namespace content
