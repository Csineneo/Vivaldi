// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_VULKAN_VULKAN_SURFACE_H_
#define GPU_VULKAN_VULKAN_SURFACE_H_

#include "base/memory/scoped_ptr.h"
#include "gpu/vulkan/vulkan_export.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/swap_result.h"

namespace gpu {

class VulkanDeviceQueue;
class VulkanSwapChain;

class VULKAN_EXPORT VulkanSurface {
 public:
  static bool InitializeOneOff();

  // Minimum bit depth of surface.
  enum Format {
    FORMAT_BGRA8888,
    FORMAT_RGB565,

    NUM_SURFACE_FORMATS,
    DEFAULT_SURFACE_FORMAT = FORMAT_BGRA8888
  };

  virtual ~VulkanSurface() = 0;

  virtual bool Initialize(VulkanDeviceQueue* device_queue,
                          VulkanSurface::Format format) = 0;
  virtual void Destroy() = 0;

  virtual gfx::SwapResult SwapBuffers() = 0;

  virtual VulkanSwapChain* GetSwapChain() = 0;

  virtual void Finish() = 0;

  // Create a surface that render directlys into a surface.
  static scoped_ptr<VulkanSurface> CreateViewSurface(
      gfx::AcceleratedWidget window);

 protected:
  VulkanSurface();

 private:
  DISALLOW_COPY_AND_ASSIGN(VulkanSurface);
};

}  // namespace gpu

#endif  // GPU_VULKAN_VULKAN_SURFACE_H_
