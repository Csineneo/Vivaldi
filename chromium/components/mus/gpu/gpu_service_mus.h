// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_GPU_GPU_SERVICE_MUS_H_
#define COMPONENTS_MUS_GPU_GPU_SERVICE_MUS_H_

#include "components/mus/public/interfaces/gpu_memory_buffer.mojom.h"
#include "components/mus/public/interfaces/gpu_service.mojom.h"
#include "gpu/ipc/common/surface_handle.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"

namespace mus {

// TODO(fsamuel): GpuServiceMus is intended to be the Gpu thread within Mus.
// Similar to GpuChildThread, it is a GpuChannelManagerDelegate and will have a
// GpuChannelManager.
class GpuServiceMus : public mojom::GpuService,
                      public gpu::GpuChannelManagerDelegate {
 public:
  GpuServiceMus();
  ~GpuServiceMus() override;

  // mojom::GpuService overrides:
  void EstablishGpuChannel(
      bool prempts,
      bool allow_view_command_buffers,
      bool allow_real_time_streams,
      const mojom::GpuService::EstablishGpuChannelCallback& callback) override;

  void CreateGpuMemoryBuffer(
      mojom::GpuMemoryBufferIdPtr id,
      mojo::SizePtr size,
      mojom::BufferFormat format,
      mojom::BufferUsage usage,
      uint64_t surface_id,
      const mojom::GpuService::CreateGpuMemoryBufferCallback& callback)
      override;

  void CreateGpuMemoryBufferFromHandle(
      mojom::GpuMemoryBufferHandlePtr buffer_handle,
      mojom::GpuMemoryBufferIdPtr id,
      mojo::SizePtr size,
      mojom::BufferFormat format,
      const mojom::GpuService::CreateGpuMemoryBufferFromHandleCallback&
          callback) override;

  void DestroyGpuMemoryBuffer(mojom::GpuMemoryBufferIdPtr id,
                              const gpu::SyncToken& sync_token) override;

  // GpuChannelManagerDelegate overrides:
  void DidCreateOffscreenContext(const GURL& active_url) override;
  void DidDestroyChannel(int client_id) override;
  void DidDestroyOffscreenContext(const GURL& active_url) override;
  void DidLoseContext(bool offscreen,
                      gpu::error::ContextLostReason reason,
                      const GURL& active_url) override;
  void GpuMemoryUmaStats(const gpu::GPUMemoryUmaStats& params) override;
  void StoreShaderToDisk(int32_t client_id,
                         const std::string& key,
                         const std::string& shader) override;
#if defined(OS_WIN)
  void SendAcceleratedSurfaceCreatedChildWindow(
      gpu::SurfaceHandle parent_window,
      gpu::SurfaceHandle child_window) override;
#endif
  void SetActiveURL(const GURL& url) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(GpuServiceMus);
};

}  // namespace mus

#endif  // COMPONENTS_MUS_GPU_GPU_SERVICE_MUS_H_
