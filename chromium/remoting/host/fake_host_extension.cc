// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/fake_host_extension.h"

#include <string>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "remoting/codec/video_encoder.h"
#include "remoting/host/host_extension_session.h"
#include "remoting/proto/control.pb.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"

namespace remoting {

class FakeExtension::Session : public HostExtensionSession {
 public:
  Session(FakeExtension* extension, const std::string& message_type);
  ~Session() override {}

  // HostExtensionSession interface.
  bool OnExtensionMessage(ClientSessionControl* client_session_control,
                          protocol::ClientStub* client_stub,
                          const protocol::ExtensionMessage& message) override;

 private:
  FakeExtension* extension_;
  std::string message_type_;

  DISALLOW_COPY_AND_ASSIGN(Session);
};

FakeExtension::Session::Session(FakeExtension* extension,
                                const std::string& message_type)
    : extension_(extension), message_type_(message_type) {}

bool FakeExtension::Session::OnExtensionMessage(
    ClientSessionControl* client_session_control,
    protocol::ClientStub* client_stub,
    const protocol::ExtensionMessage& message) {
  if (message.type() == message_type_) {
    extension_->has_handled_message_ = true;
    return true;
  }
  return false;
}

FakeExtension::FakeExtension(const std::string& message_type,
                             const std::string& capability)
    : message_type_(message_type), capability_(capability) {}

FakeExtension::~FakeExtension() {}

std::string FakeExtension::capability() const {
  return capability_;
}

std::unique_ptr<HostExtensionSession> FakeExtension::CreateExtensionSession(
    ClientSessionControl* client_session_control,
    protocol::ClientStub* client_stub) {
  DCHECK(!was_instantiated());
  was_instantiated_ = true;
  return base::WrapUnique(new Session(this, message_type_));
}

} // namespace remoting
