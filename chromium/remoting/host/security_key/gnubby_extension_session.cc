// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/security_key/gnubby_extension_session.h"

#include "base/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/values.h"
#include "remoting/base/logging.h"
#include "remoting/host/security_key/gnubby_auth_handler.h"
#include "remoting/proto/control.pb.h"
#include "remoting/protocol/client_stub.h"

namespace {

// Used as the type attribute of all Security Key protocol::ExtensionMessages.
const char kExtensionMessageType[] = "gnubby-auth";

// Gnubby extension message data members.
const char kConnectionId[] = "connectionId";
const char kControlMessage[] = "control";
const char kControlOption[] = "option";
const char kDataMessage[] = "data";
const char kDataPayload[] = "data";
const char kErrorMessage[] = "error";
const char kGnubbyAuthV1[] = "auth-v1";
const char kMessageType[] = "type";

// Returns the command code (the first byte of the data) if it exists, or -1 if
// the data is empty.
unsigned int GetCommandCode(const std::string& data) {
  return data.empty() ? -1 : static_cast<unsigned int>(data[0]);
}

// Creates a string of byte data from a ListValue of numbers. Returns true if
// all of the list elements are numbers.
bool ConvertListValueToString(base::ListValue* bytes, std::string* out) {
  out->clear();

  unsigned int byte_count = bytes->GetSize();
  if (byte_count != 0) {
    out->reserve(byte_count);
    for (unsigned int i = 0; i < byte_count; i++) {
      int value;
      if (!bytes->GetInteger(i, &value)) {
        return false;
      }
      out->push_back(static_cast<char>(value));
    }
  }
  return true;
}

}  // namespace

namespace remoting {

GnubbyExtensionSession::GnubbyExtensionSession(
    protocol::ClientStub* client_stub)
    : client_stub_(client_stub) {
  DCHECK(client_stub_);

  gnubby_auth_handler_ = remoting::GnubbyAuthHandler::Create(base::Bind(
      &GnubbyExtensionSession::SendMessageToClient, base::Unretained(this)));
}

GnubbyExtensionSession::~GnubbyExtensionSession() {}

// Returns true if the |message| is a Security Key ExtensionMessage.
// This is done so the host does not pass |message| to other HostExtensions.
// TODO(joedow): Use |client_session_control| to disconnect the session if we
//               receive an invalid extension message.
bool GnubbyExtensionSession::OnExtensionMessage(
    ClientSessionControl* client_session_control,
    protocol::ClientStub* client_stub,
    const protocol::ExtensionMessage& message) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (message.type() != kExtensionMessageType) {
    return false;
  }

  std::unique_ptr<base::Value> value = base::JSONReader::Read(message.data());
  base::DictionaryValue* client_message;
  if (!value || !value->GetAsDictionary(&client_message)) {
    LOG(WARNING) << "Failed to retrieve data from gnubby-auth message.";
    return true;
  }

  std::string type;
  if (!client_message->GetString(kMessageType, &type)) {
    LOG(WARNING) << "Invalid gnubby-auth message format.";
    return true;
  }

  if (type == kControlMessage) {
    ProcessControlMessage(client_message);
  } else if (type == kDataMessage) {
    ProcessDataMessage(client_message);
  } else if (type == kErrorMessage) {
    ProcessErrorMessage(client_message);
  } else {
    VLOG(2) << "Unknown gnubby-auth message type: " << type;
  }

  return true;
}

void GnubbyExtensionSession::ProcessControlMessage(
    base::DictionaryValue* message_data) const {
  std::string option;
  if (!message_data->GetString(kControlOption, &option)) {
    LOG(WARNING) << "Could not extract control option from message.";
    return;
  }

  if (option == kGnubbyAuthV1) {
    gnubby_auth_handler_->CreateGnubbyConnection();
  } else {
    VLOG(2) << "Invalid gnubby-auth control option: " << option;
  }
}

void GnubbyExtensionSession::ProcessDataMessage(
    base::DictionaryValue* message_data) const {
  int connection_id;
  if (!message_data->GetInteger(kConnectionId, &connection_id)) {
    LOG(WARNING) << "Could not extract connection id from message.";
    return;
  }

  if (!gnubby_auth_handler_->IsValidConnectionId(connection_id)) {
    LOG(WARNING) << "Unknown gnubby-auth data connection: '" << connection_id
                 << "'";
    return;
  }

  base::ListValue* bytes;
  std::string response;
  if (message_data->GetList(kDataPayload, &bytes) &&
      ConvertListValueToString(bytes, &response)) {
    HOST_LOG << "Sending gnubby response: " << GetCommandCode(response);
    gnubby_auth_handler_->SendClientResponse(connection_id, response);
  } else {
    LOG(WARNING) << "Could not extract response data from message.";
    gnubby_auth_handler_->SendErrorAndCloseConnection(connection_id);
    return;
  }
}

void GnubbyExtensionSession::ProcessErrorMessage(
    base::DictionaryValue* message_data) const {
  int connection_id;
  if (!message_data->GetInteger(kConnectionId, &connection_id)) {
    LOG(WARNING) << "Could not extract connection id from message.";
    return;
  }

  if (gnubby_auth_handler_->IsValidConnectionId(connection_id)) {
    HOST_LOG << "Sending gnubby error";
    gnubby_auth_handler_->SendErrorAndCloseConnection(connection_id);
  } else {
    LOG(WARNING) << "Unknown gnubby-auth data connection: '" << connection_id
                 << "'";
  }
}

void GnubbyExtensionSession::SendMessageToClient(
    int connection_id,
    const std::string& data) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(client_stub_);

  base::DictionaryValue request;
  request.SetString(kMessageType, kDataMessage);
  request.SetInteger(kConnectionId, connection_id);

  std::unique_ptr<base::ListValue> bytes(new base::ListValue());
  for (std::string::const_iterator i = data.begin(); i != data.end(); ++i) {
    bytes->AppendInteger(static_cast<unsigned char>(*i));
  }
  request.Set(kDataPayload, bytes.release());

  std::string request_json;
  CHECK(base::JSONWriter::Write(request, &request_json));

  protocol::ExtensionMessage message;
  message.set_type(kExtensionMessageType);
  message.set_data(request_json);

  client_stub_->DeliverHostMessage(message);
}

void GnubbyExtensionSession::SetGnubbyAuthHandlerForTesting(
    std::unique_ptr<GnubbyAuthHandler> gnubby_auth_handler) {
  DCHECK(gnubby_auth_handler);

  gnubby_auth_handler_ = std::move(gnubby_auth_handler);
  gnubby_auth_handler_->SetSendMessageCallback(base::Bind(
      &GnubbyExtensionSession::SendMessageToClient, base::Unretained(this)));
}

}  // namespace remoting
