// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_low_energy_win_fake.h"

#include "base/strings/stringprintf.h"
#include "device/bluetooth/bluetooth_low_energy_defs_win.h"

namespace {
const char kPlatformNotSupported[] =
    "Bluetooth Low energy is only supported on Windows 8 and later.";
}  // namespace

namespace device {
namespace win {

BLEDevice::BLEDevice() {}
BLEDevice::~BLEDevice() {}

BLEGattService::BLEGattService() {}
BLEGattService::~BLEGattService() {}

BLEGattCharacteristic::BLEGattCharacteristic() {}
BLEGattCharacteristic::~BLEGattCharacteristic() {}

BLEGattDescriptor::BLEGattDescriptor() {}
BLEGattDescriptor::~BLEGattDescriptor() {}

BluetoothLowEnergyWrapperFake::BluetoothLowEnergyWrapperFake() {}
BluetoothLowEnergyWrapperFake::~BluetoothLowEnergyWrapperFake() {}

bool BluetoothLowEnergyWrapperFake::IsBluetoothLowEnergySupported() {
  return true;
}

bool BluetoothLowEnergyWrapperFake::EnumerateKnownBluetoothLowEnergyDevices(
    ScopedVector<BluetoothLowEnergyDeviceInfo>* devices,
    std::string* error) {
  if (!IsBluetoothLowEnergySupported()) {
    *error = kPlatformNotSupported;
    return false;
  }

  for (auto& device : simulated_devices_) {
    BluetoothLowEnergyDeviceInfo* device_info =
        new BluetoothLowEnergyDeviceInfo();
    *device_info = *(device.second->device_info);
    devices->push_back(device_info);
  }
  return true;
}

bool BluetoothLowEnergyWrapperFake::
    EnumerateKnownBluetoothLowEnergyGattServiceDevices(
        ScopedVector<BluetoothLowEnergyDeviceInfo>* devices,
        std::string* error) {
  if (!IsBluetoothLowEnergySupported()) {
    *error = kPlatformNotSupported;
    return false;
  }

  for (auto& device : simulated_devices_) {
    for (auto& service : device.second->primary_services) {
      BluetoothLowEnergyDeviceInfo* device_info =
          new BluetoothLowEnergyDeviceInfo();
      *device_info = *(device.second->device_info);
      base::string16 path = GenerateBLEGattServiceDevicePath(
          device.second->device_info->path.value(),
          service.second->service_info->AttributeHandle);
      device_info->path = base::FilePath(path);
      devices->push_back(device_info);
    }
  }
  return true;
}

bool BluetoothLowEnergyWrapperFake::EnumerateKnownBluetoothLowEnergyServices(
    const base::FilePath& device_path,
    ScopedVector<BluetoothLowEnergyServiceInfo>* services,
    std::string* error) {
  if (!IsBluetoothLowEnergySupported()) {
    *error = kPlatformNotSupported;
    return false;
  }

  base::string16 device_address =
      ExtractDeviceAddressFromDevicePath(device_path.value());
  std::vector<std::string> service_attribute_handles =
      ExtractServiceAttributeHandlesFromDevicePath(device_path.value());

  BLEDevicesMap::iterator it_d = simulated_devices_.find(
      std::string(device_address.begin(), device_address.end()));
  CHECK(it_d != simulated_devices_.end());

  // |service_attribute_handles| is empty means |device_path| is a BLE device
  // path, otherwise it is a BLE GATT service device path.
  if (service_attribute_handles.empty()) {
    // Return all primary services for BLE device.
    for (auto& primary_service : it_d->second->primary_services) {
      BluetoothLowEnergyServiceInfo* service_info =
          new BluetoothLowEnergyServiceInfo();
      service_info->uuid = primary_service.second->service_info->ServiceUuid;
      service_info->attribute_handle =
          primary_service.second->service_info->AttributeHandle;
      services->push_back(service_info);
    }
  } else {
    // Return corresponding GATT service for BLE GATT service device.
    BLEGattService* target_service =
        GetSimulatedGattService(it_d->second.get(), service_attribute_handles);
    BluetoothLowEnergyServiceInfo* service_info =
        new BluetoothLowEnergyServiceInfo();
    service_info->uuid = target_service->service_info->ServiceUuid;
    service_info->attribute_handle =
        target_service->service_info->AttributeHandle;
    services->push_back(service_info);
  }

  return true;
}

HRESULT BluetoothLowEnergyWrapperFake::ReadCharacteristicsOfAService(
    base::FilePath& service_path,
    const PBTH_LE_GATT_SERVICE service,
    scoped_ptr<BTH_LE_GATT_CHARACTERISTIC>* out_included_characteristics,
    USHORT* out_counts) {
  base::string16 device_address =
      ExtractDeviceAddressFromDevicePath(service_path.value());
  const std::vector<std::string> service_att_handles =
      ExtractServiceAttributeHandlesFromDevicePath(service_path.value());
  BLEGattService* target_service = GetSimulatedGattService(
      GetSimulatedBLEDevice(
          std::string(device_address.begin(), device_address.end())),
      service_att_handles);
  if (target_service == nullptr)
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

  std::size_t number_of_included_characteristic =
      target_service->included_characteristics.size();
  if (number_of_included_characteristic) {
    *out_counts = (USHORT)number_of_included_characteristic;
    out_included_characteristics->reset(
        new BTH_LE_GATT_CHARACTERISTIC[number_of_included_characteristic]);
    std::size_t i = 0;
    for (const auto& cha : target_service->included_characteristics) {
      out_included_characteristics->get()[i] =
          *(cha.second->characteristic_info);
      i++;
    }
    return S_OK;
  }
  return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
}

BLEDevice* BluetoothLowEnergyWrapperFake::SimulateBLEDevice(
    std::string device_name,
    BLUETOOTH_ADDRESS device_address) {
  BLEDevice* device = new BLEDevice();
  BluetoothLowEnergyDeviceInfo* device_info =
      new BluetoothLowEnergyDeviceInfo();
  std::string string_device_address =
      BluetoothAddressToCanonicalString(device_address);
  device_info->path =
      base::FilePath(GenerateBLEDevicePath(string_device_address));
  device_info->friendly_name = device_name;
  device_info->address = device_address;
  device->device_info.reset(device_info);
  simulated_devices_[string_device_address] = make_scoped_ptr(device);
  return device;
}

BLEDevice* BluetoothLowEnergyWrapperFake::GetSimulatedBLEDevice(
    std::string device_address) {
  BLEDevicesMap::iterator it_d = simulated_devices_.find(device_address);
  if (it_d == simulated_devices_.end())
    return nullptr;
  return it_d->second.get();
}

BLEGattService* BluetoothLowEnergyWrapperFake::SimulateBLEGattService(
    BLEDevice* device,
    BLEGattService* parent_service,
    const BTH_LE_UUID& uuid) {
  CHECK(device);

  BLEGattService* service = new BLEGattService();
  PBTH_LE_GATT_SERVICE service_info = new BTH_LE_GATT_SERVICE[1];
  std::string string_device_address =
      BluetoothAddressToCanonicalString(device->device_info->address);
  service_info->AttributeHandle =
      GenerateAUniqueAttributeHandle(string_device_address);
  service_info->ServiceUuid = uuid;
  service->service_info.reset(service_info);

  if (parent_service) {
    parent_service
        ->included_services[std::to_string(service_info->AttributeHandle)] =
        make_scoped_ptr(service);
  } else {
    device->primary_services[std::to_string(service_info->AttributeHandle)] =
        make_scoped_ptr(service);
  }
  return service;
}

void BluetoothLowEnergyWrapperFake::SimulateBLEGattServiceRemoved(
    BLEDevice* device,
    BLEGattService* parent_service,
    std::string attribute_handle) {
  if (parent_service) {
    parent_service->included_services.erase(attribute_handle);
  } else {
    device->primary_services.erase(attribute_handle);
  }
}

BLEGattService* BluetoothLowEnergyWrapperFake::GetSimulatedGattService(
    BLEDevice* device,
    const std::vector<std::string>& chain_of_att_handle) {
  // First, find the root primary service.
  BLEGattServicesMap::iterator it_s =
      device->primary_services.find(chain_of_att_handle[0]);
  if (it_s == device->primary_services.end())
    return nullptr;

  // Iteratively follow the chain of included service attribute handles to find
  // the target service.
  for (std::size_t i = 1; i < chain_of_att_handle.size(); i++) {
    std::string included_att_handle = std::string(
        chain_of_att_handle[i].begin(), chain_of_att_handle[i].end());
    BLEGattServicesMap::iterator it_i =
        it_s->second->included_services.find(included_att_handle);
    if (it_i == it_s->second->included_services.end())
      return nullptr;
    it_s = it_i;
  }
  return it_s->second.get();
}

BLEGattCharacteristic*
BluetoothLowEnergyWrapperFake::SimulateBLEGattCharacterisc(
    std::string device_address,
    BLEGattService* parent_service,
    const BTH_LE_GATT_CHARACTERISTIC& characteristic) {
  CHECK(parent_service);

  BLEGattCharacteristic* win_characteristic = new BLEGattCharacteristic();
  PBTH_LE_GATT_CHARACTERISTIC win_characteristic_info =
      new BTH_LE_GATT_CHARACTERISTIC[1];
  *win_characteristic_info = characteristic;
  (win_characteristic->characteristic_info).reset(win_characteristic_info);
  win_characteristic->characteristic_info->AttributeHandle =
      GenerateAUniqueAttributeHandle(device_address);
  parent_service->included_characteristics[std::to_string(
      win_characteristic->characteristic_info->AttributeHandle)] =
      make_scoped_ptr(win_characteristic);
  return win_characteristic;
}

void BluetoothLowEnergyWrapperFake::SimulateBLEGattCharacteriscRemove(
    BLEGattService* parent_service,
    std::string attribute_handle) {
  CHECK(parent_service);
  parent_service->included_characteristics.erase(attribute_handle);
}

USHORT BluetoothLowEnergyWrapperFake::GenerateAUniqueAttributeHandle(
    std::string device_address) {
  scoped_ptr<std::set<USHORT>>& set_of_ushort =
      attribute_handle_table_[device_address];
  if (set_of_ushort) {
    USHORT max_attribute_handle = *set_of_ushort->rbegin();
    if (max_attribute_handle < 0xFFFF) {
      USHORT new_attribute_handle = max_attribute_handle + 1;
      set_of_ushort->insert(new_attribute_handle);
      return new_attribute_handle;
    } else {
      USHORT i = 1;
      for (; i < 0xFFFF; i++) {
        if (set_of_ushort->find(i) == set_of_ushort->end())
          break;
      }
      if (i >= 0xFFFF)
        return 0;
      set_of_ushort->insert(i);
      return i;
    }
  }

  USHORT smallest_att_handle = 1;
  std::set<USHORT>* new_set = new std::set<USHORT>();
  new_set->insert(smallest_att_handle);
  set_of_ushort.reset(new_set);
  return smallest_att_handle;
}

base::string16 BluetoothLowEnergyWrapperFake::GenerateBLEDevicePath(
    std::string device_address) {
  return base::string16(device_address.begin(), device_address.end());
}

base::string16 BluetoothLowEnergyWrapperFake::GenerateBLEGattServiceDevicePath(
    base::string16 resident_device_path,
    USHORT service_attribute_handle) {
  std::string sub_path = std::to_string(service_attribute_handle);
  return resident_device_path + L"/" +
         base::string16(sub_path.begin(), sub_path.end());
}

base::string16
BluetoothLowEnergyWrapperFake::ExtractDeviceAddressFromDevicePath(
    base::string16 path) {
  std::size_t found = path.find_first_of('/');
  if (found != base::string16::npos) {
    return path.substr(0, found);
  }
  return path;
}

std::vector<std::string>
BluetoothLowEnergyWrapperFake::ExtractServiceAttributeHandlesFromDevicePath(
    base::string16 path) {
  std::size_t found = path.find('/');
  if (found == base::string16::npos)
    return std::vector<std::string>();

  std::vector<std::string> chain_of_att_handle;
  while (true) {
    std::size_t next_found = path.find(path, found + 1);
    if (next_found == base::string16::npos)
      break;
    base::string16 att_handle = path.substr(found + 1, next_found);
    chain_of_att_handle.push_back(
        std::string(att_handle.begin(), att_handle.end()));
    found = next_found;
  }
  base::string16 att_handle = path.substr(found + 1);
  chain_of_att_handle.push_back(
      std::string(att_handle.begin(), att_handle.end()));
  return chain_of_att_handle;
}

std::string BluetoothLowEnergyWrapperFake::BluetoothAddressToCanonicalString(
    const BLUETOOTH_ADDRESS& btha) {
  std::string result = base::StringPrintf(
      "%02X:%02X:%02X:%02X:%02X:%02X", btha.rgBytes[5], btha.rgBytes[4],
      btha.rgBytes[3], btha.rgBytes[2], btha.rgBytes[1], btha.rgBytes[0]);
  return result;
}

}  // namespace win
}  // namespace device
