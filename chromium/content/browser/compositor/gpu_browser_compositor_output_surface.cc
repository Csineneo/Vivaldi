// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/gpu_browser_compositor_output_surface.h"

#include <utility>

#include "build/build_config.h"
#include "cc/output/output_surface_client.h"
#include "cc/output/output_surface_frame.h"
#include "components/display_compositor/compositor_overlay_candidate_validator.h"
#include "content/browser/compositor/reflector_impl.h"
#include "content/browser/compositor/reflector_texture.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/gpu/client/context_provider_command_buffer.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/ipc/client/command_buffer_proxy_impl.h"

namespace content {

GpuBrowserCompositorOutputSurface::GpuBrowserCompositorOutputSurface(
    scoped_refptr<ContextProviderCommandBuffer> context,
    scoped_refptr<ui::CompositorVSyncManager> vsync_manager,
    cc::SyntheticBeginFrameSource* begin_frame_source,
    std::unique_ptr<display_compositor::CompositorOverlayCandidateValidator>
        overlay_candidate_validator)
    : BrowserCompositorOutputSurface(std::move(context),
                                     std::move(vsync_manager),
                                     begin_frame_source,
                                     std::move(overlay_candidate_validator)),
      swap_buffers_completion_callback_(base::Bind(
          &GpuBrowserCompositorOutputSurface::OnGpuSwapBuffersCompleted,
          base::Unretained(this))),
      update_vsync_parameters_callback_(base::Bind(
          &BrowserCompositorOutputSurface::OnUpdateVSyncParametersFromGpu,
          base::Unretained(this))) {}

GpuBrowserCompositorOutputSurface::~GpuBrowserCompositorOutputSurface() {}

gpu::CommandBufferProxyImpl*
GpuBrowserCompositorOutputSurface::GetCommandBufferProxy() {
  ContextProviderCommandBuffer* provider_command_buffer =
      static_cast<content::ContextProviderCommandBuffer*>(
          context_provider_.get());
  gpu::CommandBufferProxyImpl* command_buffer_proxy =
      provider_command_buffer->GetCommandBufferProxy();
  DCHECK(command_buffer_proxy);
  return command_buffer_proxy;
}

void GpuBrowserCompositorOutputSurface::OnReflectorChanged() {
  if (!reflector_) {
    reflector_texture_.reset();
  } else {
    reflector_texture_.reset(new ReflectorTexture(context_provider()));
    reflector_->OnSourceTextureMailboxUpdated(reflector_texture_->mailbox());
  }
}

bool GpuBrowserCompositorOutputSurface::BindToClient(
    cc::OutputSurfaceClient* client) {
  if (!BrowserCompositorOutputSurface::BindToClient(client))
    return false;

  GetCommandBufferProxy()->SetSwapBuffersCompletionCallback(
      swap_buffers_completion_callback_.callback());
  GetCommandBufferProxy()->SetUpdateVSyncParametersCallback(
      update_vsync_parameters_callback_.callback());
  if (capabilities_.uses_default_gl_framebuffer) {
    capabilities_.flipped_output_surface =
        context_provider()->ContextCapabilities().flips_vertically;
  }
  return true;
}

void GpuBrowserCompositorOutputSurface::EnsureBackbuffer() {}

void GpuBrowserCompositorOutputSurface::DiscardBackbuffer() {
  context_provider()->ContextGL()->DiscardBackbufferCHROMIUM();
}

void GpuBrowserCompositorOutputSurface::BindFramebuffer() {
  context_provider()->ContextGL()->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GpuBrowserCompositorOutputSurface::SwapBuffers(
    cc::OutputSurfaceFrame frame) {
  GetCommandBufferProxy()->SetLatencyInfo(frame.latency_info);

  gfx::Rect swap_rect = frame.sub_buffer_rect;
  gfx::Size surface_size = frame.size;
  if (reflector_) {
    if (swap_rect == gfx::Rect(surface_size)) {
      reflector_texture_->CopyTextureFullImage(surface_size);
      reflector_->OnSourceSwapBuffers(surface_size);
    } else {
      reflector_texture_->CopyTextureSubImage(swap_rect);
      reflector_->OnSourcePostSubBuffer(swap_rect, surface_size);
    }
  }

  if (swap_rect == gfx::Rect(frame.size))
    context_provider_->ContextSupport()->Swap();
  else
    context_provider_->ContextSupport()->PartialSwapBuffers(swap_rect);
}

uint32_t GpuBrowserCompositorOutputSurface::GetFramebufferCopyTextureFormat() {
  auto* gl = static_cast<ContextProviderCommandBuffer*>(context_provider());
  return gl->GetCopyTextureInternalFormat();
}

bool GpuBrowserCompositorOutputSurface::IsDisplayedAsOverlayPlane() const {
  return false;
}

unsigned GpuBrowserCompositorOutputSurface::GetOverlayTextureId() const {
  return 0;
}

bool GpuBrowserCompositorOutputSurface::SurfaceIsSuspendForRecycle() const {
  return false;
}

void GpuBrowserCompositorOutputSurface::OnGpuSwapBuffersCompleted(
    const std::vector<ui::LatencyInfo>& latency_info,
    gfx::SwapResult result,
    const gpu::GpuProcessHostedCALayerTreeParamsMac* params_mac) {
  RenderWidgetHostImpl::CompositorFrameDrawn(latency_info);
  client_->DidSwapBuffersComplete();
}

#if defined(OS_MACOSX)
void GpuBrowserCompositorOutputSurface::SetSurfaceSuspendedForRecycle(
    bool suspended) {}
#endif

}  // namespace content
