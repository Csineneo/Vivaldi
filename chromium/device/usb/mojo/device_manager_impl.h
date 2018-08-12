// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_USB_MOJO_DEVICE_MANAGER_IMPL_H_
#define DEVICE_USB_MOJO_DEVICE_MANAGER_IMPL_H_

#include <memory>
#include <queue>
#include <set>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "device/usb/public/interfaces/device_manager.mojom.h"
#include "device/usb/usb_service.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace device {

class UsbDevice;

namespace usb {

class PermissionProvider;

// Implementation of the public DeviceManager interface. This interface can be
// requested from the devices app located at "devices", if available.
class DeviceManagerImpl : public DeviceManager, public UsbService::Observer {
 public:
  static void Create(base::WeakPtr<PermissionProvider> permission_provider,
                     DeviceManagerRequest request);

  ~DeviceManagerImpl() override;

 private:
  DeviceManagerImpl(base::WeakPtr<PermissionProvider> permission_provider,
                    UsbService* usb_service);

  // DeviceManager implementation:
  void GetDevices(EnumerationOptionsPtr options,
                  const GetDevicesCallback& callback) override;
  void GetDevice(const std::string& guid,
                 DeviceRequest device_request) override;
  void SetClient(DeviceManagerClientPtr client) override;

  // Callbacks to handle the async responses from the underlying UsbService.
  void OnGetDevices(EnumerationOptionsPtr options,
                    const GetDevicesCallback& callback,
                    const std::vector<scoped_refptr<UsbDevice>>& devices);

  // UsbService::Observer implementation:
  void OnDeviceAdded(scoped_refptr<UsbDevice> device) override;
  void OnDeviceRemoved(scoped_refptr<UsbDevice> device) override;
  void WillDestroyUsbService() override;

  void MaybeRunDeviceChangesCallback();

  mojo::StrongBindingPtr<DeviceManager> binding_;
  base::WeakPtr<PermissionProvider> permission_provider_;

  UsbService* usb_service_;
  ScopedObserver<UsbService, UsbService::Observer> observer_;
  DeviceManagerClientPtr client_;

  base::WeakPtrFactory<DeviceManagerImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceManagerImpl);
};

}  // namespace usb
}  // namespace device

#endif  // DEVICE_USB_MOJO_DEVICE_MANAGER_IMPL_H_
