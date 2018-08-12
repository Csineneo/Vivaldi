// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_WIN_H_
#define DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_WIN_H_

#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner.h"
#include "device/bluetooth/bluetooth_gatt_characteristic.h"
#include "device/bluetooth/bluetooth_low_energy_defs_win.h"

namespace device {

class BluetoothAdapterWin;
class BluetoothRemoteGattServiceWin;
class BluetoothTaskManagerWin;

// The BluetoothRemoteGattCharacteristicWin class implements
// BluetoothGattCharacteristic for remote GATT services on Windows 8 and later.
class DEVICE_BLUETOOTH_EXPORT BluetoothRemoteGattCharacteristicWin
    : public BluetoothGattCharacteristic {
 public:
  BluetoothRemoteGattCharacteristicWin(
      BluetoothRemoteGattServiceWin* parent_service,
      BTH_LE_GATT_CHARACTERISTIC* characteristic_info,
      scoped_refptr<base::SequencedTaskRunner>& ui_task_runner);
  ~BluetoothRemoteGattCharacteristicWin() override;

  // Override BluetoothGattCharacteristic interfaces.
  std::string GetIdentifier() const override;
  BluetoothUUID GetUUID() const override;
  bool IsLocal() const override;
  std::vector<uint8_t>& GetValue() const override;
  BluetoothGattService* GetService() const override;
  Properties GetProperties() const override;
  Permissions GetPermissions() const override;
  bool IsNotifying() const override;
  std::vector<BluetoothGattDescriptor*> GetDescriptors() const override;
  BluetoothGattDescriptor* GetDescriptor(
      const std::string& identifier) const override;
  bool AddDescriptor(BluetoothGattDescriptor* descriptor) override;
  bool UpdateValue(const std::vector<uint8_t>& value) override;
  void StartNotifySession(const NotifySessionCallback& callback,
                          const ErrorCallback& error_callback) override;
  void ReadRemoteCharacteristic(const ValueCallback& callback,
                                const ErrorCallback& error_callback) override;
  void WriteRemoteCharacteristic(const std::vector<uint8_t>& new_value,
                                 const base::Closure& callback,
                                 const ErrorCallback& error_callback) override;

  // Update included descriptors.
  void Update();
  uint16_t GetAttributeHandle() const;

 private:
  BluetoothAdapterWin* adapter_;
  BluetoothRemoteGattServiceWin* parent_service_;
  scoped_refptr<BluetoothTaskManagerWin> task_manager_;

  // Characteristic info from OS and used to interact with OS.
  scoped_ptr<BTH_LE_GATT_CHARACTERISTIC> characteristic_info_;
  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  BluetoothUUID characteristic_uuid_;
  std::vector<uint8_t> characteristic_value_;
  std::string characteristic_identifier_;

  base::WeakPtrFactory<BluetoothRemoteGattCharacteristicWin> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(BluetoothRemoteGattCharacteristicWin);
};

}  // namespace device

#endif  // DEVICE_BLUETOOTH_BLUETOOTH_REMOTE_GATT_CHARACTERISTIC_WIN_H_
