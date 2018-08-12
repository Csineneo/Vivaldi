// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mash/browser_driver/browser_driver_application_delegate.h"

#include <stdint.h>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "components/mus/public/cpp/event_matcher.h"
#include "mojo/shell/public/cpp/connection.h"
#include "mojo/shell/public/cpp/connector.h"

namespace mash {
namespace browser_driver {
namespace {

enum class Accelerator : uint32_t {
  NewWindow,
  NewTab,
  NewIncognitoWindow,
};

struct AcceleratorSpec {
  Accelerator id;
  mus::mojom::KeyboardCode keyboard_code;
  // A bitfield of kEventFlag* and kMouseEventFlag* values in
  // input_event_constants.mojom.
  int event_flags;
};

AcceleratorSpec g_spec[] = {
    {Accelerator::NewWindow, mus::mojom::KeyboardCode::N,
     mus::mojom::kEventFlagControlDown},
    {Accelerator::NewTab, mus::mojom::KeyboardCode::T,
     mus::mojom::kEventFlagControlDown},
    {Accelerator::NewIncognitoWindow, mus::mojom::KeyboardCode::N,
     mus::mojom::kEventFlagControlDown | mus::mojom::kEventFlagShiftDown},
};

void AssertTrue(bool success) {
  DCHECK(success);
}

void DoNothing() {}

}  // namespace

BrowserDriverApplicationDelegate::BrowserDriverApplicationDelegate()
    : connector_(nullptr),
      binding_(this),
      weak_factory_(this) {}

BrowserDriverApplicationDelegate::~BrowserDriverApplicationDelegate() {}

void BrowserDriverApplicationDelegate::Initialize(
    mojo::Connector* connector,
    const mojo::Identity& identity,
    uint32_t id) {
  connector_ = connector;
  AddAccelerators();
}

bool BrowserDriverApplicationDelegate::AcceptConnection(
    mojo::Connection* connection) {
  return true;
}

bool BrowserDriverApplicationDelegate::ShellConnectionLost() {
  // Prevent the code in AddAccelerators() from keeping this app alive.
  binding_.set_connection_error_handler(base::Bind(&DoNothing));
  return true;
}

void BrowserDriverApplicationDelegate::OnAccelerator(
    uint32_t id, mus::mojom::EventPtr event) {
  switch (static_cast<Accelerator>(id)) {
    case Accelerator::NewWindow:
    case Accelerator::NewTab:
    case Accelerator::NewIncognitoWindow:
      connector_->Connect("exe:chrome");
      // TODO(beng): have Chrome export a service that allows it to be driven by
      //             this driver, e.g. to open new tabs, incognito windows, etc.
      break;
    default:
      NOTREACHED();
      break;
  }
}

void BrowserDriverApplicationDelegate::AddAccelerators() {
  // TODO(beng): find some other way to get the window manager. I don't like
  //             having to specify it by URL because it may differ per display.
  mus::mojom::AcceleratorRegistrarPtr registrar;
  connector_->ConnectToInterface("mojo:desktop_wm", &registrar);

  if (binding_.is_bound())
    binding_.Unbind();
  registrar->SetHandler(binding_.CreateInterfacePtrAndBind());
  // If the window manager restarts, the handler pipe will close and we'll need
  // to re-add our accelerators when the window manager comes back up.
  binding_.set_connection_error_handler(
      base::Bind(&BrowserDriverApplicationDelegate::AddAccelerators,
                 weak_factory_.GetWeakPtr()));

  for (const AcceleratorSpec& spec : g_spec) {
    registrar->AddAccelerator(
        static_cast<uint32_t>(spec.id),
        mus::CreateKeyMatcher(spec.keyboard_code, spec.event_flags),
        base::Bind(&AssertTrue));
  }
}

}  // namespace browser_driver
}  // namespace main
