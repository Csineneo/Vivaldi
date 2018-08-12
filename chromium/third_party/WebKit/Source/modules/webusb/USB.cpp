// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/webusb/USB.h"

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/dom/DOMException.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "device/usb/public/interfaces/device.mojom-blink.h"
#include "modules/EventTargetModules.h"
#include "modules/webusb/USBConnectionEvent.h"
#include "modules/webusb/USBDevice.h"
#include "modules/webusb/USBDeviceFilter.h"
#include "modules/webusb/USBDeviceRequestOptions.h"
#include "platform/UserGestureIndicator.h"
#include "platform/mojo/MojoHelper.h"
#include "platform/wtf/Functional.h"
#include "public/platform/InterfaceProvider.h"
#include "public/platform/Platform.h"

namespace usb = device::usb::blink;

namespace blink {
namespace {

const char kNoServiceError[] = "USB service unavailable.";

usb::DeviceFilterPtr ConvertDeviceFilter(const USBDeviceFilter& filter) {
  auto mojo_filter = usb::DeviceFilter::New();
  mojo_filter->has_vendor_id = filter.hasVendorId();
  if (mojo_filter->has_vendor_id)
    mojo_filter->vendor_id = filter.vendorId();
  mojo_filter->has_product_id = filter.hasProductId();
  if (mojo_filter->has_product_id)
    mojo_filter->product_id = filter.productId();
  mojo_filter->has_class_code = filter.hasClassCode();
  if (mojo_filter->has_class_code)
    mojo_filter->class_code = filter.classCode();
  mojo_filter->has_subclass_code = filter.hasSubclassCode();
  if (mojo_filter->has_subclass_code)
    mojo_filter->subclass_code = filter.subclassCode();
  mojo_filter->has_protocol_code = filter.hasProtocolCode();
  if (mojo_filter->has_protocol_code)
    mojo_filter->protocol_code = filter.protocolCode();
  if (filter.hasSerialNumber())
    mojo_filter->serial_number = filter.serialNumber();
  return mojo_filter;
}

}  // namespace

USB::USB(LocalFrame& frame)
    : ContextLifecycleObserver(frame.GetDocument()), client_binding_(this) {}

USB::~USB() {
  // |m_deviceManager| and |m_chooserService| may still be valid but there
  // should be no more outstanding requests to them because each holds a
  // persistent handle to this object.
  DCHECK(device_manager_requests_.IsEmpty());
  DCHECK(chooser_service_requests_.IsEmpty());
}

void USB::Dispose() {
  // The pipe to this object must be closed when is marked unreachable to
  // prevent messages from being dispatched before lazy sweeping.
  client_binding_.Close();
}

ScriptPromise USB::getDevices(ScriptState* script_state) {
  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();
  EnsureDeviceManagerConnection();
  if (!device_manager_) {
    resolver->Reject(DOMException::Create(kNotSupportedError));
  } else {
    device_manager_requests_.insert(resolver);
    device_manager_->GetDevices(
        nullptr, ConvertToBaseCallback(WTF::Bind(&USB::OnGetDevices,
                                                 WrapPersistent(this),
                                                 WrapPersistent(resolver))));
  }
  return promise;
}

ScriptPromise USB::requestDevice(ScriptState* script_state,
                                 const USBDeviceRequestOptions& options) {
  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();

  if (!chooser_service_) {
    if (!GetFrame()) {
      resolver->Reject(DOMException::Create(kNotSupportedError));
      return promise;
    }
    GetFrame()->GetInterfaceProvider()->GetInterface(
        mojo::MakeRequest(&chooser_service_));
    chooser_service_.set_connection_error_handler(
        ConvertToBaseCallback(WTF::Bind(&USB::OnChooserServiceConnectionError,
                                        WrapWeakPersistent(this))));
  }

  if (!UserGestureIndicator::ConsumeUserGesture()) {
    resolver->Reject(DOMException::Create(
        kSecurityError,
        "Must be handling a user gesture to show a permission request."));
  } else {
    Vector<usb::DeviceFilterPtr> filters;
    if (options.hasFilters()) {
      filters.ReserveCapacity(options.filters().size());
      for (const auto& filter : options.filters())
        filters.push_back(ConvertDeviceFilter(filter));
    }
    chooser_service_requests_.insert(resolver);
    chooser_service_->GetPermission(
        std::move(filters), ConvertToBaseCallback(WTF::Bind(
                                &USB::OnGetPermission, WrapPersistent(this),
                                WrapPersistent(resolver))));
  }
  return promise;
}

ExecutionContext* USB::GetExecutionContext() const {
  return ContextLifecycleObserver::GetExecutionContext();
}

const AtomicString& USB::InterfaceName() const {
  return EventTargetNames::USB;
}

void USB::ContextDestroyed(ExecutionContext*) {
  device_manager_.reset();
  device_manager_requests_.Clear();
  chooser_service_.reset();
  chooser_service_requests_.Clear();
}

USBDevice* USB::GetOrCreateDevice(usb::DeviceInfoPtr device_info) {
  USBDevice* device = device_cache_.at(device_info->guid);
  if (!device) {
    String guid = device_info->guid;
    usb::DevicePtr pipe;
    device_manager_->GetDevice(guid, mojo::MakeRequest(&pipe));
    device = USBDevice::Create(std::move(device_info), std::move(pipe),
                               GetExecutionContext());
    device_cache_.insert(guid, device);
  }
  return device;
}

void USB::OnGetDevices(ScriptPromiseResolver* resolver,
                       Vector<usb::DeviceInfoPtr> device_infos) {
  auto request_entry = device_manager_requests_.Find(resolver);
  if (request_entry == device_manager_requests_.end())
    return;
  device_manager_requests_.erase(request_entry);

  HeapVector<Member<USBDevice>> devices;
  for (auto& device_info : device_infos)
    devices.push_back(GetOrCreateDevice(std::move(device_info)));
  resolver->Resolve(devices);
  device_manager_requests_.erase(resolver);
}

void USB::OnGetPermission(ScriptPromiseResolver* resolver,
                          usb::DeviceInfoPtr device_info) {
  auto request_entry = chooser_service_requests_.Find(resolver);
  if (request_entry == chooser_service_requests_.end())
    return;
  chooser_service_requests_.erase(request_entry);

  EnsureDeviceManagerConnection();
  if (!device_manager_) {
    resolver->Reject(DOMException::Create(kNotFoundError, kNoServiceError));
    return;
  }

  if (device_info) {
    resolver->Resolve(GetOrCreateDevice(std::move(device_info)));
  } else {
    resolver->Reject(
        DOMException::Create(kNotFoundError, "No device selected."));
  }
}

void USB::OnDeviceAdded(usb::DeviceInfoPtr device_info) {
  if (!device_manager_)
    return;

  DispatchEvent(USBConnectionEvent::Create(
      EventTypeNames::connect, GetOrCreateDevice(std::move(device_info))));
}

void USB::OnDeviceRemoved(usb::DeviceInfoPtr device_info) {
  String guid = device_info->guid;
  USBDevice* device = device_cache_.at(guid);
  if (!device) {
    device = USBDevice::Create(std::move(device_info), nullptr,
                               GetExecutionContext());
  }
  DispatchEvent(USBConnectionEvent::Create(EventTypeNames::disconnect, device));
  device_cache_.erase(guid);
}

void USB::OnDeviceManagerConnectionError() {
  device_manager_.reset();
  client_binding_.Close();
  for (ScriptPromiseResolver* resolver : device_manager_requests_)
    resolver->Reject(DOMException::Create(kNotFoundError, kNoServiceError));
  device_manager_requests_.Clear();
}

void USB::OnChooserServiceConnectionError() {
  chooser_service_.reset();
  for (ScriptPromiseResolver* resolver : chooser_service_requests_)
    resolver->Reject(DOMException::Create(kNotFoundError, kNoServiceError));
  chooser_service_requests_.Clear();
}

void USB::AddedEventListener(const AtomicString& event_type,
                             RegisteredEventListener& listener) {
  EventTargetWithInlineData::AddedEventListener(event_type, listener);
  if (event_type == EventTypeNames::connect ||
      event_type == EventTypeNames::disconnect) {
    EnsureDeviceManagerConnection();
  }
}

void USB::EnsureDeviceManagerConnection() {
  if (device_manager_ || !GetFrame())
    return;

  GetFrame()->GetInterfaceProvider()->GetInterface(
      mojo::MakeRequest(&device_manager_));
  device_manager_.set_connection_error_handler(ConvertToBaseCallback(WTF::Bind(
      &USB::OnDeviceManagerConnectionError, WrapWeakPersistent(this))));

  DCHECK(!client_binding_.is_bound());
  device_manager_->SetClient(client_binding_.CreateInterfacePtrAndBind());
}

DEFINE_TRACE(USB) {
  visitor->Trace(device_manager_requests_);
  visitor->Trace(chooser_service_requests_);
  visitor->Trace(device_cache_);
  EventTargetWithInlineData::Trace(visitor);
  ContextLifecycleObserver::Trace(visitor);
}

}  // namespace blink
