// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/test/bluetooth_test_win.h"

#include "base/bind.h"
#include "base/strings/sys_string_conversions.h"
#include "device/bluetooth/bluetooth_adapter_win.h"
#include "device/bluetooth/bluetooth_low_energy_win.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic_win.h"
#include "device/bluetooth/bluetooth_remote_gatt_service_win.h"

namespace {

BLUETOOTH_ADDRESS CanonicalStringToBLUETOOTH_ADDRESS(
    std::string device_address) {
  BLUETOOTH_ADDRESS win_addr;
  unsigned int data[6];
  int result =
      sscanf_s(device_address.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
               &data[5], &data[4], &data[3], &data[2], &data[1], &data[0]);
  CHECK(result == 6);
  for (int i = 0; i < 6; i++) {
    win_addr.rgBytes[i] = data[i];
  }
  return win_addr;
}

// The canonical UUID string format is device::BluetoothUUID.value().
BTH_LE_UUID CanonicalStringToBTH_LE_UUID(std::string uuid) {
  BTH_LE_UUID win_uuid = {0};
  if (uuid.size() == 4) {
    win_uuid.IsShortUuid = TRUE;
    unsigned int data[1];
    int result = sscanf_s(uuid.c_str(), "%04x", &data[0]);
    CHECK(result == 1);
    win_uuid.Value.ShortUuid = data[0];
  } else if (uuid.size() == 36) {
    win_uuid.IsShortUuid = FALSE;
    unsigned int data[11];
    int result = sscanf_s(
        uuid.c_str(), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        &data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &data[6],
        &data[7], &data[8], &data[9], &data[10]);
    CHECK(result == 11);
    win_uuid.Value.LongUuid.Data1 = data[0];
    win_uuid.Value.LongUuid.Data2 = data[1];
    win_uuid.Value.LongUuid.Data3 = data[2];
    win_uuid.Value.LongUuid.Data4[0] = data[3];
    win_uuid.Value.LongUuid.Data4[1] = data[4];
    win_uuid.Value.LongUuid.Data4[2] = data[5];
    win_uuid.Value.LongUuid.Data4[3] = data[6];
    win_uuid.Value.LongUuid.Data4[4] = data[7];
    win_uuid.Value.LongUuid.Data4[5] = data[8];
    win_uuid.Value.LongUuid.Data4[6] = data[9];
    win_uuid.Value.LongUuid.Data4[7] = data[10];
  } else {
    CHECK(false);
  }

  return win_uuid;
}

}  // namespace

namespace device {
BluetoothTestWin::BluetoothTestWin()
    : ui_task_runner_(new base::TestSimpleTaskRunner()),
      bluetooth_task_runner_(new base::TestSimpleTaskRunner()),
      adapter_win_(nullptr),
      fake_bt_classic_wrapper_(nullptr),
      fake_bt_le_wrapper_(nullptr) {}
BluetoothTestWin::~BluetoothTestWin() {}

bool BluetoothTestWin::PlatformSupportsLowEnergy() {
  if (fake_bt_le_wrapper_)
    return fake_bt_le_wrapper_->IsBluetoothLowEnergySupported();
  return true;
}

void BluetoothTestWin::AdapterInitCallback() {}

void BluetoothTestWin::InitWithDefaultAdapter() {
  adapter_ = new BluetoothAdapterWin(base::Bind(
      &BluetoothTestWin::AdapterInitCallback, base::Unretained(this)));
  adapter_win_ = static_cast<BluetoothAdapterWin*>(adapter_.get());
  adapter_win_->Init();
}

void BluetoothTestWin::InitWithoutDefaultAdapter() {
  adapter_ = new BluetoothAdapterWin(base::Bind(
      &BluetoothTestWin::AdapterInitCallback, base::Unretained(this)));
  adapter_win_ = static_cast<BluetoothAdapterWin*>(adapter_.get());
  adapter_win_->InitForTest(ui_task_runner_, bluetooth_task_runner_);
}

void BluetoothTestWin::InitWithFakeAdapter() {
  fake_bt_classic_wrapper_ = new win::BluetoothClassicWrapperFake();
  fake_bt_le_wrapper_ = new win::BluetoothLowEnergyWrapperFake();
  win::BluetoothClassicWrapper::SetInstanceForTest(fake_bt_classic_wrapper_);
  win::BluetoothLowEnergyWrapper::SetInstanceForTest(fake_bt_le_wrapper_);
  fake_bt_classic_wrapper_->SimulateARadio(
      base::SysUTF8ToWide(kTestAdapterName),
      CanonicalStringToBLUETOOTH_ADDRESS(kTestAdapterAddress));

  adapter_ = new BluetoothAdapterWin(base::Bind(
      &BluetoothTestWin::AdapterInitCallback, base::Unretained(this)));
  adapter_win_ = static_cast<BluetoothAdapterWin*>(adapter_.get());
  adapter_win_->InitForTest(ui_task_runner_, bluetooth_task_runner_);
  bluetooth_task_runner_->RunPendingTasks();
  ui_task_runner_->RunPendingTasks();
}

bool BluetoothTestWin::DenyPermission() {
  return false;
}

void BluetoothTestWin::StartLowEnergyDiscoverySession() {
  __super ::StartLowEnergyDiscoverySession();
  bluetooth_task_runner_->RunPendingTasks();
  ui_task_runner_->RunPendingTasks();
}

BluetoothDevice* BluetoothTestWin::DiscoverLowEnergyDevice(int device_ordinal) {
  if (device_ordinal > 4 || device_ordinal < 1)
    return nullptr;

  std::string device_name = kTestDeviceName;
  std::string device_address = kTestDeviceAddress1;
  std::string service_uuid_1;
  std::string service_uuid_2;

  switch (device_ordinal) {
    case 1: {
      service_uuid_1 = kTestUUIDGenericAccess;
      service_uuid_2 = kTestUUIDGenericAttribute;
    } break;
    case 2: {
      service_uuid_1 = kTestUUIDImmediateAlert;
      service_uuid_2 = kTestUUIDLinkLoss;
    } break;
    case 3: {
      device_name = kTestDeviceNameEmpty;
    } break;
    case 4: {
      device_name = kTestDeviceNameEmpty;
      device_address = kTestDeviceAddress2;
    } break;
  }

  win::BLEDevice* simulated_device = fake_bt_le_wrapper_->SimulateBLEDevice(
      device_name, CanonicalStringToBLUETOOTH_ADDRESS(device_address));
  if (simulated_device != nullptr) {
    if (!service_uuid_1.empty()) {
      fake_bt_le_wrapper_->SimulateBLEGattService(
          simulated_device, nullptr,
          CanonicalStringToBTH_LE_UUID(service_uuid_1));
    }
    if (!service_uuid_2.empty()) {
      fake_bt_le_wrapper_->SimulateBLEGattService(
          simulated_device, nullptr,
          CanonicalStringToBTH_LE_UUID(service_uuid_2));
    }
  }
  bluetooth_task_runner_->RunPendingTasks();
  ui_task_runner_->RunPendingTasks();

  std::vector<BluetoothDevice*> devices = adapter_win_->GetDevices();
  for (auto device : devices) {
    if (device->GetAddress() == device_address)
      return device;
  }

  return nullptr;
}

void BluetoothTestWin::SimulateGattConnection(BluetoothDevice* device) {
  bluetooth_task_runner_->RunPendingTasks();
  ui_task_runner_->RunPendingTasks();

  // Clear records caused by CreateGattConnection since we do not support it on
  // Windows.
  gatt_discovery_attempts_++;
  expected_success_callback_calls_--;
  unexpected_error_callback_ = false;
}

void BluetoothTestWin::SimulateGattServicesDiscovered(
    BluetoothDevice* device,
    const std::vector<std::string>& uuids) {
  win::BLEDevice* simulated_device =
      fake_bt_le_wrapper_->GetSimulatedBLEDevice(device->GetAddress());
  CHECK(simulated_device);

  for (auto uuid : uuids) {
    fake_bt_le_wrapper_->SimulateBLEGattService(
        simulated_device, nullptr, CanonicalStringToBTH_LE_UUID(uuid));
  }

  bluetooth_task_runner_->RunPendingTasks();
  ui_task_runner_->RunPendingTasks();
}

void BluetoothTestWin::SimulateGattServiceRemoved(
    BluetoothGattService* service) {
  std::string device_address = service->GetDevice()->GetAddress();
  win::BLEDevice* target_device =
      fake_bt_le_wrapper_->GetSimulatedBLEDevice(device_address);
  CHECK(target_device);

  BluetoothRemoteGattServiceWin* win_service =
      static_cast<BluetoothRemoteGattServiceWin*>(service);
  std::string service_att_handle =
      std::to_string(win_service->GetAttributeHandle());
  fake_bt_le_wrapper_->SimulateBLEGattServiceRemoved(target_device, nullptr,
                                                     service_att_handle);

  ForceRefreshDevice();
}

void BluetoothTestWin::SimulateGattCharacteristic(BluetoothGattService* service,
                                                  const std::string& uuid,
                                                  int properties) {
  std::string device_address = service->GetDevice()->GetAddress();
  win::BLEDevice* target_device =
      fake_bt_le_wrapper_->GetSimulatedBLEDevice(device_address);
  CHECK(target_device);
  win::BLEGattService* target_service =
      GetSimulatedService(target_device, service);
  CHECK(target_service);

  BTH_LE_GATT_CHARACTERISTIC win_cha_info;
  win_cha_info.CharacteristicUuid = CanonicalStringToBTH_LE_UUID(uuid);
  if (properties & BluetoothGattCharacteristic::PROPERTY_BROADCAST)
    win_cha_info.IsBroadcastable = TRUE;
  if (properties & BluetoothGattCharacteristic::PROPERTY_READ)
    win_cha_info.IsReadable = TRUE;
  if (properties & BluetoothGattCharacteristic::PROPERTY_WRITE_WITHOUT_RESPONSE)
    win_cha_info.IsWritableWithoutResponse = TRUE;
  if (properties & BluetoothGattCharacteristic::PROPERTY_WRITE)
    win_cha_info.IsWritable = TRUE;
  if (properties & BluetoothGattCharacteristic::PROPERTY_NOTIFY)
    win_cha_info.IsNotifiable = TRUE;
  if (properties & BluetoothGattCharacteristic::PROPERTY_INDICATE)
    win_cha_info.IsIndicatable = TRUE;
  if (properties &
      BluetoothGattCharacteristic::PROPERTY_AUTHENTICATED_SIGNED_WRITES)
    win_cha_info.IsSignedWritable = TRUE;
  if (properties & BluetoothGattCharacteristic::PROPERTY_EXTENDED_PROPERTIES)
    win_cha_info.HasExtendedProperties = TRUE;
  fake_bt_le_wrapper_->SimulateBLEGattCharacterisc(
      device_address, target_service, win_cha_info);

  ForceRefreshDevice();
}

void BluetoothTestWin::SimulateGattCharacteristicRemoved(
    BluetoothGattService* service,
    BluetoothGattCharacteristic* characteristic) {
  CHECK(service);
  CHECK(characteristic);

  std::string device_address = service->GetDevice()->GetAddress();
  win::BLEGattService* target_service = GetSimulatedService(
      fake_bt_le_wrapper_->GetSimulatedBLEDevice(device_address), service);
  CHECK(target_service);

  std::string characteristic_att_handle = std::to_string(
      static_cast<BluetoothRemoteGattCharacteristicWin*>(characteristic)
          ->GetAttributeHandle());
  fake_bt_le_wrapper_->SimulateBLEGattCharacteriscRemove(
      target_service, characteristic_att_handle);

  ForceRefreshDevice();
}

win::BLEGattService* BluetoothTestWin::GetSimulatedService(
    win::BLEDevice* device,
    BluetoothGattService* service) {
  CHECK(device);
  CHECK(service);

  std::vector<std::string> chain_of_att_handles;
  BluetoothRemoteGattServiceWin* win_service =
      static_cast<BluetoothRemoteGattServiceWin*>(service);
  chain_of_att_handles.insert(
      chain_of_att_handles.begin(),
      std::to_string(win_service->GetAttributeHandle()));
  win::BLEGattService* simulated_service =
      fake_bt_le_wrapper_->GetSimulatedGattService(device,
                                                   chain_of_att_handles);
  CHECK(simulated_service);
  return simulated_service;
}

void BluetoothTestWin::ForceRefreshDevice() {
  adapter_win_->force_update_device_for_test_ = true;
  bluetooth_task_runner_->RunPendingTasks();
  ui_task_runner_->RunPendingTasks();
}
}
