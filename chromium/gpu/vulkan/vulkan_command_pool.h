// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_VULKAN_VULKAN_COMMAND_POOL_H_
#define GPU_VULKAN_VULKAN_COMMAND_POOL_H_

#include <vulkan/vulkan.h>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"

namespace gpu {

class VulkanCommandBuffer;
class VulkanDeviceQueue;

class VulkanCommandPool {
 public:
  explicit VulkanCommandPool(VulkanDeviceQueue* device_queue);
  ~VulkanCommandPool();

  bool Initialize();
  void Destroy();

  scoped_ptr<VulkanCommandBuffer> CreatePrimaryCommandBuffer();
  scoped_ptr<VulkanCommandBuffer> CreateSecondaryCommandBuffer();

  VkCommandPool handle() { return handle_; }

 private:
  friend class VulkanCommandBuffer;

  void IncrementCommandBufferCount();
  void DecrementCommandBufferCount();

  VulkanDeviceQueue* device_queue_;
  VkCommandPool handle_ = VK_NULL_HANDLE;
  uint32_t command_buffer_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(VulkanCommandPool);
};

}  // namespace gpu

#endif  // GPU_VULKAN_VULKAN_COMMAND_POOL_H_
