// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_BLUETOOTH_BLUETOOTH_DEVICE_H_
#define CONTENT_COMMON_BLUETOOTH_BLUETOOTH_DEVICE_H_

#include <stdint.h>

#include <string>

#include "base/strings/string16.h"
#include "content/common/content_export.h"
#include "device/bluetooth/bluetooth_device.h"

namespace content {

// Data sent over IPC representing a Bluetooth device, corresponding to
// blink::WebBluetoothDevice.
struct CONTENT_EXPORT BluetoothDevice {
  BluetoothDevice();
  BluetoothDevice(const std::string& id,
                  const base::string16& name,
                  const std::vector<std::string>& uuids);
  ~BluetoothDevice();

  static std::vector<std::string> UUIDsFromBluetoothUUIDs(
      const device::BluetoothDevice::UUIDList& uuid_list);

  std::string id;
  base::string16 name;
  std::vector<std::string> uuids;  // 128bit UUIDs with dashes. 36 chars.
};

}  // namespace content

#endif  // CONTENT_COMMON_BLUETOOTH_BLUETOOTH_DEVICE_H_
