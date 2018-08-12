// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mash/wm/root_window_controller.h"

#include <stdint.h>

#include "base/bind.h"
#include "components/mus/common/util.h"
#include "components/mus/public/cpp/event_matcher.h"
#include "components/mus/public/cpp/window.h"
#include "components/mus/public/cpp/window_tree_connection.h"
#include "components/mus/public/cpp/window_tree_host_factory.h"
#include "mash/session/public/interfaces/session.mojom.h"
#include "mash/wm/background_layout.h"
#include "mash/wm/fill_layout.h"
#include "mash/wm/screenlock_layout.h"
#include "mash/wm/shadow_controller.h"
#include "mash/wm/shelf_layout.h"
#include "mash/wm/window_layout.h"
#include "mash/wm/window_manager.h"
#include "mash/wm/window_manager_application.h"
#include "mojo/shell/public/cpp/connector.h"

namespace mash {
namespace wm {
namespace {

const uint32_t kWindowSwitchAccelerator = 1;

void AssertTrue(bool success) {
  DCHECK(success);
}

}  // namespace

// static
RootWindowController* RootWindowController::CreateFromDisplay(
    WindowManagerApplication* app,
    mus::mojom::DisplayPtr display,
    mojo::InterfaceRequest<mus::mojom::WindowTreeClient> client_request) {
  RootWindowController* controller = new RootWindowController(app);
  controller->display_ = std::move(display);
  mus::WindowTreeConnection::CreateForWindowManager(
      controller, std::move(client_request),
      mus::WindowTreeConnection::CreateType::DONT_WAIT_FOR_EMBED,
      controller->window_manager_.get());
  return controller;
}

void RootWindowController::Destroy() {
  // See class description for details on lifetime.
  if (root_) {
    delete root_->connection();
  } else {
    // This case only happens if we're destroyed before OnEmbed().
    delete this;
  }
}

mojo::Connector* RootWindowController::GetConnector() {
  return app_->connector();
}

mus::Window* RootWindowController::GetWindowForContainer(
    mojom::Container container) {
  const mus::Id window_id = root_->connection()->GetConnectionId() << 16 |
                            static_cast<uint16_t>(container);
  return root_->GetChildById(window_id);
}

mus::Window* RootWindowController::GetWindowById(mus::Id id) {
  return root_->GetChildById(id);
}

bool RootWindowController::WindowIsContainer(const mus::Window* window) const {
  return window && window->parent() == root_;
}

mus::WindowManagerClient* RootWindowController::window_manager_client() {
  return window_manager_->window_manager_client();
}

void RootWindowController::OnAccelerator(uint32_t id, const ui::Event& event) {
  switch (id) {
    case kWindowSwitchAccelerator:
      window_manager_client()->ActivateNextWindow();
      break;
    default:
      app_->OnAccelerator(id, event);
      break;
  }
}

RootWindowController::RootWindowController(WindowManagerApplication* app)
    : app_(app), root_(nullptr), window_count_(0) {
  window_manager_.reset(new WindowManager);
}

RootWindowController::~RootWindowController() {}

void RootWindowController::AddAccelerators() {
  window_manager_client()->AddAccelerator(
      kWindowSwitchAccelerator,
      mus::CreateKeyMatcher(mus::mojom::KeyboardCode::TAB,
                            mus::mojom::kEventFlagControlDown),
      base::Bind(&AssertTrue));
}

void RootWindowController::OnEmbed(mus::Window* root) {
  root_ = root;
  root_->AddObserver(this);
  layout_manager_[root_].reset(new FillLayout(root_));

  app_->OnRootWindowControllerGotRoot(this);

  CreateContainers();

  // Override the default layout managers for certain containers.
  mus::Window* user_background =
      GetWindowForContainer(mojom::Container::USER_BACKGROUND);
  layout_manager_[user_background].reset(new BackgroundLayout(user_background));
  mus::Window* login_app = GetWindowForContainer(mojom::Container::LOGIN_APP);
  layout_manager_[login_app].reset(new ScreenlockLayout(login_app));
  mus::Window* user_shelf = GetWindowForContainer(mojom::Container::USER_SHELF);
  layout_manager_[user_shelf].reset(new ShelfLayout(user_shelf));

  mus::Window* window = GetWindowForContainer(mojom::Container::USER_WINDOWS);
  layout_manager_[window].reset(new WindowLayout(window));
  window_manager_client()->AddActivationParent(window);

  // Bubble windows must be allowed to activate because some of them rely on
  // deactivation to close.
  mus::Window* bubbles = GetWindowForContainer(mojom::Container::BUBBLES);
  window_manager_client()->AddActivationParent(bubbles);

  AddAccelerators();

  window_manager_->Initialize(this, app_->session());

  shadow_controller_.reset(new ShadowController(root->connection()));

  app_->OnRootWindowControllerDoneInit(this);
}

void RootWindowController::OnConnectionLost(
    mus::WindowTreeConnection* connection) {
  shadow_controller_.reset();
  delete this;
}

void RootWindowController::OnWindowDestroyed(mus::Window* window) {
  DCHECK_EQ(window, root_);
  app_->OnRootWindowDestroyed(this);
  root_->RemoveObserver(this);
  // Delete the |window_manager_| here so that WindowManager doesn't have to
  // worry about the possibility of |root_| being null.
  window_manager_.reset();
  root_ = nullptr;
}

void RootWindowController::CreateContainer(
    mash::wm::mojom::Container container,
    mash::wm::mojom::Container parent_container) {
  mus::Window* window = root_->connection()->NewWindow();
  DCHECK_EQ(mus::LoWord(window->id()), static_cast<uint16_t>(container))
      << "Containers must be created before other windows!";
  // Install a FillLayout by default for containers.
  layout_manager_[window].reset(new FillLayout(window));
  // User private windows are hidden by default until the window manager learns
  // the lock state, so their contents are never accidentally revealed.
  window->SetVisible(container != mojom::Container::USER_PRIVATE);
  mus::Id parent_id = (mus::HiWord(window->id()) << 16) |
                      static_cast<uint16_t>(parent_container);
  mus::Window* parent = parent_container == mojom::Container::ROOT
                            ? root_
                            : root_->GetChildById(parent_id);
  parent->AddChild(window);
}

void RootWindowController::CreateContainers() {
  CreateContainer(mojom::Container::ALL_USER_BACKGROUND,
                  mojom::Container::ROOT);
  CreateContainer(mojom::Container::USER_WORKSPACE, mojom::Container::ROOT);
  CreateContainer(mojom::Container::USER_BACKGROUND,
                  mojom::Container::USER_WORKSPACE);
  CreateContainer(mojom::Container::USER_PRIVATE,
                  mojom::Container::USER_WORKSPACE);
  CreateContainer(mojom::Container::USER_WINDOWS,
                  mojom::Container::USER_PRIVATE);
  CreateContainer(mojom::Container::USER_STICKY_WINDOWS,
                  mojom::Container::USER_PRIVATE);
  CreateContainer(mojom::Container::USER_PRESENTATION_WINDOWS,
                  mojom::Container::USER_PRIVATE);
  CreateContainer(mojom::Container::USER_SHELF, mojom::Container::USER_PRIVATE);
  CreateContainer(mojom::Container::LOGIN_WINDOWS, mojom::Container::ROOT);
  CreateContainer(mojom::Container::LOGIN_APP, mojom::Container::LOGIN_WINDOWS);
  CreateContainer(mojom::Container::LOGIN_SHELF,
                  mojom::Container::LOGIN_WINDOWS);
  CreateContainer(mojom::Container::BUBBLES, mojom::Container::ROOT);
  CreateContainer(mojom::Container::SYSTEM_MODAL_WINDOWS,
                  mojom::Container::ROOT);
  CreateContainer(mojom::Container::KEYBOARD, mojom::Container::ROOT);
  CreateContainer(mojom::Container::MENUS, mojom::Container::ROOT);
  CreateContainer(mojom::Container::TOOLTIPS, mojom::Container::ROOT);
}

}  // namespace wm
}  // namespace mash
