// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BLUETOOTH_CACHE_QUERY_RESULT_H_
#define CONTENT_BROWSER_BLUETOOTH_CACHE_QUERY_RESULT_H_

#include "content/browser/bluetooth/bluetooth_metrics.h"
#include "content/common/bluetooth/bluetooth_messages.h"

namespace device {
class BluetoothDevice;
class BluetoothRemoteGattService;
class BluetoothRemoteGattCharacteristic;
}

namespace content {

// Struct that holds the result of a cache query.
//
// WebBluetoothServiceImpl and BluetoothDispatcherHost have functions
// that return a CacheQueryResult so we make a separate header for it.
// TODO(ortuno): Move into WebBluetoothServiceImpl once we move all functions
// off BluetoothDispatcherHost.
// https://crbug.com/508771
struct CacheQueryResult {
  CacheQueryResult() : outcome(CacheQueryOutcome::SUCCESS) {}

  explicit CacheQueryResult(CacheQueryOutcome outcome) : outcome(outcome) {}

  ~CacheQueryResult() {}

  blink::WebBluetoothError GetWebError() const {
    switch (outcome) {
      case CacheQueryOutcome::SUCCESS:
      case CacheQueryOutcome::BAD_RENDERER:
        NOTREACHED();
        return blink::WebBluetoothError::DEVICE_NO_LONGER_IN_RANGE;
      case CacheQueryOutcome::NO_DEVICE:
        return blink::WebBluetoothError::DEVICE_NO_LONGER_IN_RANGE;
      case CacheQueryOutcome::NO_SERVICE:
        return blink::WebBluetoothError::SERVICE_NO_LONGER_EXISTS;
      case CacheQueryOutcome::NO_CHARACTERISTIC:
        return blink::WebBluetoothError::CHARACTERISTIC_NO_LONGER_EXISTS;
    }
    NOTREACHED();
    return blink::WebBluetoothError::DEVICE_NO_LONGER_IN_RANGE;
  }

  device::BluetoothDevice* device = nullptr;
  device::BluetoothRemoteGattService* service = nullptr;
  device::BluetoothRemoteGattCharacteristic* characteristic = nullptr;
  CacheQueryOutcome outcome;
};

}  // namespace content

#endif  // CONTENT_BROWSER_BLUETOOTH_CACHE_QUERY_RESULT_H_
