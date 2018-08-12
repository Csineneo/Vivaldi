// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_MOCK_DEVICE_VIDEO_CAPTURE_SERVICE_TEST_H_
#define SERVICES_VIDEO_CAPTURE_MOCK_DEVICE_VIDEO_CAPTURE_SERVICE_TEST_H_

#include "services/shell/public/cpp/service_test.h"
#include "services/video_capture/mock_device_descriptor_receiver.h"
#include "services/video_capture/mock_video_capture_device_client.h"
#include "services/video_capture/mock_video_capture_device_impl.h"
#include "services/video_capture/public/interfaces/video_capture_service.mojom.h"

namespace video_capture {

// Reusable test setup for testing with a single mock device.
class MockDeviceVideoCaptureServiceTest : public shell::test::ServiceTest {
 public:
  MockDeviceVideoCaptureServiceTest();
  ~MockDeviceVideoCaptureServiceTest() override;

  void SetUp() override;

 protected:
  mojom::VideoCaptureServicePtr service_;
  mojom::VideoCaptureDeviceFactoryPtr factory_;
  MockDeviceDescriptorReceiver descriptor_receiver_;

  std::unique_ptr<MockVideoCaptureDeviceImpl> mock_device_;
  std::unique_ptr<MockVideoCaptureDeviceClient> mock_client_;
  mojom::MockVideoCaptureDevicePtr mock_device_proxy_;
  mojom::VideoCaptureDeviceProxyPtr device_proxy_;
  mojom::VideoCaptureDeviceClientPtr mock_client_proxy_;
  media::VideoCaptureFormat requested_format_;
};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_MOCK_DEVICE_VIDEO_CAPTURE_SERVICE_TEST_H_
