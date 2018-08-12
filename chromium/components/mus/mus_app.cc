// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mus/mus_app.h"

#include <set>

#include "base/stl_util.h"
#include "build/build_config.h"
#include "base/threading/platform_thread.h"
#include "components/mus/common/args.h"
#include "components/mus/gles2/gpu_impl.h"
#include "components/mus/ws/client_connection.h"
#include "components/mus/ws/connection_manager.h"
#include "components/mus/ws/window_tree_factory.h"
#include "components/mus/ws/window_tree_host_connection.h"
#include "components/mus/ws/window_tree_host_impl.h"
#include "components/mus/ws/window_tree_impl.h"
#include "components/resource_provider/public/cpp/resource_loader.h"
#include "mojo/public/c/system/main.h"
#include "mojo/services/tracing/public/cpp/tracing_impl.h"
#include "mojo/shell/public/cpp/connection.h"
#include "mojo/shell/public/cpp/connector.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/events/event_switches.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/gl/gl_surface.h"

#if defined(USE_X11)
#include <X11/Xlib.h>
#include "base/command_line.h"
#include "ui/platform_window/x11/x11_window.h"
#elif defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

using mojo::Connection;
using mojo::InterfaceRequest;
using mus::mojom::WindowTreeHostFactory;
using mus::mojom::Gpu;

namespace mus {

namespace {

const char kResourceFileStrings[] = "mus_app_resources_strings.pak";
const char kResourceFile100[] = "mus_app_resources_100.pak";
const char kResourceFile200[] = "mus_app_resources_200.pak";

}  // namespace

// TODO(sky): this is a pretty typical pattern, make it easier to do.
struct MandolineUIServicesApp::PendingRequest {
  scoped_ptr<mojo::InterfaceRequest<mojom::DisplayManager>> dm_request;
  scoped_ptr<mojo::InterfaceRequest<mojom::WindowTreeFactory>> wtf_request;
};

MandolineUIServicesApp::MandolineUIServicesApp()
    : connector_(nullptr) {}

MandolineUIServicesApp::~MandolineUIServicesApp() {
  if (gpu_state_)
    gpu_state_->StopThreads();
  // Destroy |connection_manager_| first, since it depends on |event_source_|.
  connection_manager_.reset();
}

void MandolineUIServicesApp::InitializeResources(mojo::Connector* connector) {
  if (ui::ResourceBundle::HasSharedInstance())
    return;

  std::set<std::string> resource_paths;
  resource_paths.insert(kResourceFileStrings);
  resource_paths.insert(kResourceFile100);
  resource_paths.insert(kResourceFile200);

  resource_provider::ResourceLoader resource_loader(connector, resource_paths);
  if (!resource_loader.BlockUntilLoaded())
    return;
  CHECK(resource_loader.loaded());
  ui::RegisterPathProvider();

  // Initialize resource bundle with 1x and 2x cursor bitmaps.
  ui::ResourceBundle::InitSharedInstanceWithPakFileRegion(
      resource_loader.ReleaseFile(kResourceFileStrings),
      base::MemoryMappedFile::Region::kWholeFile);
  ui::ResourceBundle::GetSharedInstance().AddDataPackFromFile(
      resource_loader.ReleaseFile(kResourceFile100), ui::SCALE_FACTOR_100P);
  ui::ResourceBundle::GetSharedInstance().AddDataPackFromFile(
      resource_loader.ReleaseFile(kResourceFile200), ui::SCALE_FACTOR_200P);
}

void MandolineUIServicesApp::Initialize(mojo::Connector* connector,
                                        const std::string& url,
                                        uint32_t id,
                                        uint32_t user_id) {
  connector_ = connector;
  surfaces_state_ = new SurfacesState;

  base::PlatformThread::SetName("mus");

#if defined(USE_X11)
  XInitThreads();
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kUseX11TestConfig)) {
    ui::test::SetUseOverrideRedirectWindowByDefault(true);
  }
#endif

  InitializeResources(connector);

#if defined(USE_OZONE)
  // The ozone platform can provide its own event source. So initialize the
  // platform before creating the default event source.
  // TODO(rjkroege): Add tracing here.
  // Because GL libraries need to be initialized before entering the sandbox,
  // in MUS, |InitializeForUI| will load the GL libraries.
  ui::OzonePlatform::InitializeForUI();
#endif

// TODO(rjkroege): Enter sandbox here before we start threads in GpuState
// http://crbug.com/584532

#if !defined(OS_ANDROID)
  event_source_ = ui::PlatformEventSource::CreateDefault();
#endif

  // TODO(rjkroege): It is possible that we might want to generalize the
  // GpuState object.
  gpu_state_ = new GpuState();
  connection_manager_.reset(new ws::ConnectionManager(this, surfaces_state_));

  tracing_.Initialize(connector, url);
}

bool MandolineUIServicesApp::AcceptConnection(Connection* connection) {
  connection->AddInterface<Gpu>(this);
  connection->AddInterface<mojom::DisplayManager>(this);
  connection->AddInterface<mojom::WindowManagerFactoryService>(this);
  connection->AddInterface<mojom::WindowTreeFactory>(this);
  connection->AddInterface<WindowTreeHostFactory>(this);
  return true;
}

void MandolineUIServicesApp::OnFirstRootConnectionCreated() {
  PendingRequests requests;
  requests.swap(pending_requests_);
  for (auto& request : requests) {
    if (request->dm_request)
      Create(nullptr, std::move(*request->dm_request));
    else
      Create(nullptr, std::move(*request->wtf_request));
  }
}

void MandolineUIServicesApp::OnNoMoreRootConnections() {
  base::MessageLoop::current()->QuitWhenIdle();
}

ws::ClientConnection*
MandolineUIServicesApp::CreateClientConnectionForEmbedAtWindow(
    ws::ConnectionManager* connection_manager,
    mojo::InterfaceRequest<mojom::WindowTree> tree_request,
    ws::ServerWindow* root,
    uint32_t policy_bitmask,
    mojom::WindowTreeClientPtr client) {
  scoped_ptr<ws::WindowTreeImpl> service(
      new ws::WindowTreeImpl(connection_manager, root, policy_bitmask));
  return new ws::DefaultClientConnection(std::move(service), connection_manager,
                                         std::move(tree_request),
                                         std::move(client));
}

void MandolineUIServicesApp::Create(
    mojo::Connection* connection,
    mojo::InterfaceRequest<mojom::DisplayManager> request) {
  if (!connection_manager_->has_tree_host_connections()) {
    scoped_ptr<PendingRequest> pending_request(new PendingRequest);
    pending_request->dm_request.reset(
        new mojo::InterfaceRequest<mojom::DisplayManager>(std::move(request)));
    pending_requests_.push_back(std::move(pending_request));
    return;
  }
  connection_manager_->AddDisplayManagerBinding(std::move(request));
}

void MandolineUIServicesApp::Create(
    mojo::Connection* connection,
    mojo::InterfaceRequest<mojom::WindowManagerFactoryService> request) {
  connection_manager_->CreateWindowManagerFactoryService(std::move(request));
}

void MandolineUIServicesApp::Create(
    Connection* connection,
    InterfaceRequest<mojom::WindowTreeFactory> request) {
  if (!connection_manager_->has_tree_host_connections()) {
    scoped_ptr<PendingRequest> pending_request(new PendingRequest);
    pending_request->wtf_request.reset(
        new mojo::InterfaceRequest<mojom::WindowTreeFactory>(
            std::move(request)));
    pending_requests_.push_back(std::move(pending_request));
    return;
  }
  if (!window_tree_factory_) {
    window_tree_factory_.reset(
        new ws::WindowTreeFactory(connection_manager_.get()));
  }
  window_tree_factory_->AddBinding(std::move(request));
}

void MandolineUIServicesApp::Create(
    Connection* connection,
    InterfaceRequest<WindowTreeHostFactory> request) {
  factory_bindings_.AddBinding(this, std::move(request));
}

void MandolineUIServicesApp::Create(mojo::Connection* connection,
                                    mojo::InterfaceRequest<Gpu> request) {
  DCHECK(gpu_state_);
  new GpuImpl(std::move(request), gpu_state_);
}

void MandolineUIServicesApp::CreateWindowTreeHost(
    mojo::InterfaceRequest<mojom::WindowTreeHost> host,
    mojom::WindowTreeClientPtr tree_client) {
  DCHECK(connection_manager_);

  // TODO(fsamuel): We need to make sure that only the window manager can create
  // new roots.
  ws::WindowTreeHostImpl* host_impl = new ws::WindowTreeHostImpl(
      connection_manager_.get(), connector_, gpu_state_, surfaces_state_);

  // WindowTreeHostConnection manages its own lifetime.
  host_impl->Init(new ws::WindowTreeHostConnectionImpl(
      std::move(host), make_scoped_ptr(host_impl), std::move(tree_client),
      connection_manager_.get()));
}

}  // namespace mus
