// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/mojo/mojo_shell_context.h"

#include <utility>

#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/common/gpu/gpu_process_launch_causes.h"
#include "content/common/mojo/mojo_shell_connection_impl.h"
#include "content/common/mojo/static_application_loader.h"
#include "content/common/process_control.mojom.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/utility_process_host.h"
#include "content/public/browser/utility_process_host_client.h"
#include "content/public/common/content_client.h"
#include "content/public/common/service_registry.h"
#include "mojo/common/url_type_converters.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/string.h"
#include "mojo/shell/application_loader.h"
#include "mojo/shell/connect_params.h"
#include "mojo/shell/identity.h"
#include "mojo/shell/native_runner.h"
#include "mojo/shell/public/cpp/shell_client.h"
#include "mojo/shell/public/interfaces/shell.mojom.h"
#include "mojo/shell/runner/host/in_process_native_runner.h"

namespace content {

namespace {

const char kBrowserAppUrl[] = "exe:chrome";

// An extra set of apps to register on initialization, if set by a test.
const MojoShellContext::StaticApplicationMap* g_applications_for_test;

void StartUtilityProcessOnIOThread(
    mojo::InterfaceRequest<ProcessControl> request,
    const base::string16& process_name,
    bool use_sandbox) {
  UtilityProcessHost* process_host =
      UtilityProcessHost::Create(nullptr, nullptr);
  process_host->SetName(process_name);
  if (!use_sandbox)
    process_host->DisableSandbox();
  process_host->StartMojoMode();

  ServiceRegistry* services = process_host->GetServiceRegistry();
  services->ConnectToRemoteService(std::move(request));
}

void OnApplicationLoaded(const GURL& url, bool success) {
  if (!success)
    LOG(ERROR) << "Failed to launch Mojo application for " << url.spec();
}

// The default loader to use for all applications. This does nothing but drop
// the Application request.
class DefaultApplicationLoader : public mojo::shell::ApplicationLoader {
 public:
  DefaultApplicationLoader() {}
  ~DefaultApplicationLoader() override {}

 private:
  // mojo::shell::ApplicationLoader:
  void Load(const GURL& url,
            mojo::InterfaceRequest<mojo::shell::mojom::ShellClient> request)
                override {}

  DISALLOW_COPY_AND_ASSIGN(DefaultApplicationLoader);
};

// This launches a utility process and forwards the Load request the
// ProcessControl service there. The utility process is sandboxed iff
// |use_sandbox| is true.
class UtilityProcessLoader : public mojo::shell::ApplicationLoader {
 public:
  UtilityProcessLoader(const base::string16& process_name, bool use_sandbox)
      : process_name_(process_name), use_sandbox_(use_sandbox) {}
  ~UtilityProcessLoader() override {}

 private:
  // mojo::shell::ApplicationLoader:
  void Load(const GURL& url,
            mojo::InterfaceRequest<mojo::shell::mojom::ShellClient> request)
                override {
    ProcessControlPtr process_control;
    auto process_request = mojo::GetProxy(&process_control);
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            base::Bind(&StartUtilityProcessOnIOThread,
                                       base::Passed(&process_request),
                                       process_name_, use_sandbox_));
    process_control->LoadApplication(url.spec(), std::move(request),
                                     base::Bind(&OnApplicationLoaded, url));
  }

  const base::string16 process_name_;
  const bool use_sandbox_;

  DISALLOW_COPY_AND_ASSIGN(UtilityProcessLoader);
};

// Request ProcessControl from GPU process host. Must be called on IO thread.
void RequestGpuProcessControl(mojo::InterfaceRequest<ProcessControl> request) {
  BrowserChildProcessHostDelegate* process_host =
      GpuProcessHost::Get(GpuProcessHost::GPU_PROCESS_KIND_SANDBOXED,
                          CAUSE_FOR_GPU_LAUNCH_MOJO_SETUP);
  if (!process_host) {
    DLOG(ERROR) << "GPU process host not available.";
    return;
  }

  // TODO(xhwang): It's possible that |process_host| is non-null, but the actual
  // process is dead. In that case, |request| will be dropped and application
  // load requests through ProcessControl will also fail. Make sure we handle
  // these cases correctly.
  process_host->GetServiceRegistry()->ConnectToRemoteService(
      std::move(request));
}

// Forwards the load request to the GPU process.
class GpuProcessLoader : public mojo::shell::ApplicationLoader {
 public:
  GpuProcessLoader() {}
  ~GpuProcessLoader() override {}

 private:
  // mojo::shell::ApplicationLoader:
  void Load(const GURL& url,
            mojo::InterfaceRequest<mojo::shell::mojom::ShellClient> request)
                override {
    ProcessControlPtr process_control;
    auto process_request = mojo::GetProxy(&process_control);
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&RequestGpuProcessControl, base::Passed(&process_request)));
    process_control->LoadApplication(url.spec(), std::move(request),
                                     base::Bind(&OnApplicationLoaded, url));
  }

  DISALLOW_COPY_AND_ASSIGN(GpuProcessLoader);
};

}  // namespace

// Thread-safe proxy providing access to the shell context from any thread.
class MojoShellContext::Proxy {
 public:
  Proxy(MojoShellContext* shell_context)
      : shell_context_(shell_context),
        task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

  ~Proxy() {}

  void ConnectToApplication(
      const GURL& url,
      const GURL& requestor_url,
      mojo::shell::mojom::InterfaceProviderRequest request,
      mojo::shell::mojom::InterfaceProviderPtr exposed_services,
      const mojo::shell::mojom::Connector::ConnectCallback& callback) {
    if (task_runner_ == base::ThreadTaskRunnerHandle::Get()) {
      if (shell_context_) {
        shell_context_->ConnectToApplicationOnOwnThread(
            url, requestor_url, std::move(request), std::move(exposed_services),
            callback);
      }
    } else {
      // |shell_context_| outlives the main MessageLoop, so it's safe for it to
      // be unretained here.
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&MojoShellContext::ConnectToApplicationOnOwnThread,
                     base::Unretained(shell_context_), url, requestor_url,
                     base::Passed(&request), base::Passed(&exposed_services),
                     callback));
    }
  }

 private:
  MojoShellContext* shell_context_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(Proxy);
};

// static
base::LazyInstance<scoped_ptr<MojoShellContext::Proxy>>
    MojoShellContext::proxy_ = LAZY_INSTANCE_INITIALIZER;

void MojoShellContext::SetApplicationsForTest(
    const StaticApplicationMap* apps) {
  g_applications_for_test = apps;
}

MojoShellContext::MojoShellContext() {
  proxy_.Get().reset(new Proxy(this));

  scoped_refptr<base::SingleThreadTaskRunner> file_task_runner =
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE);
  bool register_mojo_url_schemes = false;

  scoped_ptr<mojo::shell::NativeRunnerFactory> native_runner_factory(
      new mojo::shell::InProcessNativeRunnerFactory(
          BrowserThread::GetBlockingPool()));
  application_manager_.reset(new mojo::shell::ApplicationManager(
      std::move(native_runner_factory), file_task_runner.get(),
      register_mojo_url_schemes, nullptr));

  application_manager_->set_default_loader(
      scoped_ptr<mojo::shell::ApplicationLoader>(new DefaultApplicationLoader));

  StaticApplicationMap apps;
  GetContentClient()->browser()->RegisterInProcessMojoApplications(&apps);
  if (g_applications_for_test) {
    // Add testing apps to the map, potentially overwriting whatever the
    // browser client registered.
    for (const auto& entry : *g_applications_for_test)
      apps[entry.first] = entry.second;
  }
  for (const auto& entry : apps) {
    application_manager_->SetLoaderForURL(
        scoped_ptr<mojo::shell::ApplicationLoader>(
            new StaticApplicationLoader(entry.second)),
        entry.first);
  }

  ContentBrowserClient::OutOfProcessMojoApplicationMap sandboxed_apps;
  GetContentClient()
      ->browser()
      ->RegisterOutOfProcessMojoApplications(&sandboxed_apps);
  for (const auto& app : sandboxed_apps) {
    application_manager_->SetLoaderForURL(
        scoped_ptr<mojo::shell::ApplicationLoader>(
            new UtilityProcessLoader(app.second, true /* use_sandbox */)),
        app.first);
  }

  ContentBrowserClient::OutOfProcessMojoApplicationMap unsandboxed_apps;
  GetContentClient()
      ->browser()
      ->RegisterUnsandboxedOutOfProcessMojoApplications(&unsandboxed_apps);
  for (const auto& app : unsandboxed_apps) {
    application_manager_->SetLoaderForURL(
        scoped_ptr<mojo::shell::ApplicationLoader>(
            new UtilityProcessLoader(app.second, false /* use_sandbox */)),
        app.first);
  }

#if (ENABLE_MOJO_MEDIA_IN_GPU_PROCESS)
  application_manager_->SetLoaderForURL(
      scoped_ptr<mojo::shell::ApplicationLoader>(new GpuProcessLoader()),
      GURL("mojo:media"));
#endif

  if (!IsRunningInMojoShell()) {
    MojoShellConnectionImpl::Create(
        application_manager_->InitInstanceForEmbedder(GURL(kBrowserAppUrl)));
  }
}

MojoShellContext::~MojoShellContext() {
  if (!IsRunningInMojoShell())
    MojoShellConnectionImpl::Destroy();
}

// static
void MojoShellContext::ConnectToApplication(
    const GURL& url,
    const GURL& requestor_url,
    mojo::shell::mojom::InterfaceProviderRequest request,
    mojo::shell::mojom::InterfaceProviderPtr exposed_services,
    const mojo::shell::mojom::Connector::ConnectCallback& callback) {
  proxy_.Get()->ConnectToApplication(url, requestor_url, std::move(request),
                                     std::move(exposed_services), callback);
}

void MojoShellContext::ConnectToApplicationOnOwnThread(
    const GURL& url,
    const GURL& requestor_url,
    mojo::shell::mojom::InterfaceProviderRequest request,
    mojo::shell::mojom::InterfaceProviderPtr exposed_services,
    const mojo::shell::mojom::Connector::ConnectCallback& callback) {
  scoped_ptr<mojo::shell::ConnectParams> params(new mojo::shell::ConnectParams);
  // TODO(beng): kUserRoot is obviously wrong.
  // TODO(beng): We need to set a permissive filter here temporarily because
  //             content is known as a bogus system: URL that the application
  //             manager doesn't understand.
  mojo::shell::Identity source_id(
      requestor_url, std::string(), mojo::shell::mojom::Connector::kUserRoot);
  source_id.SetFilter(mojo::shell::GetPermissiveCapabilityFilter());
  params->set_source(source_id);
  params->set_target(mojo::shell::Identity(
      url, std::string(), mojo::shell::mojom::Connector::kUserRoot));
  params->set_remote_interfaces(std::move(request));
  params->set_local_interfaces(std::move(exposed_services));
  params->set_connect_callback(callback);
  application_manager_->Connect(std::move(params));
}

}  // namespace content
