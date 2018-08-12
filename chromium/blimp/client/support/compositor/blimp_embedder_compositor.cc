// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/client/support/compositor/blimp_embedder_compositor.h"

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/threading/thread_task_runner_handle.h"
#include "blimp/client/public/compositor/compositor_dependencies.h"
#include "blimp/client/support/compositor/blimp_context_provider.h"
#include "cc/animation/animation_host.h"
#include "cc/layers/layer.h"
#include "cc/output/output_surface.h"
#include "cc/output/output_surface_frame.h"
#include "cc/output/texture_mailbox_deleter.h"
#include "cc/raster/single_thread_task_graph_runner.h"
#include "cc/surfaces/direct_compositor_frame_sink.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/trees/layer_tree_host_in_process.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_lib.h"

namespace blimp {
namespace client {

namespace {

class SimpleTaskGraphRunner : public cc::SingleThreadTaskGraphRunner {
 public:
  SimpleTaskGraphRunner() {
    Start("BlimpBrowserCompositorWorker",
          base::SimpleThread::Options(base::ThreadPriority::BACKGROUND));
  }

  ~SimpleTaskGraphRunner() override { Shutdown(); }
};

class DisplayOutputSurface : public cc::OutputSurface {
 public:
  explicit DisplayOutputSurface(
      scoped_refptr<cc::ContextProvider> context_provider)
      : cc::OutputSurface(std::move(context_provider)) {}

  ~DisplayOutputSurface() override = default;

  // cc::OutputSurface implementation
  void EnsureBackbuffer() override {}
  void DiscardBackbuffer() override {
    context_provider()->ContextGL()->DiscardBackbufferCHROMIUM();
  }
  void BindFramebuffer() override {
    context_provider()->ContextGL()->BindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  void SwapBuffers(cc::OutputSurfaceFrame frame) override {
    // See cc::OutputSurface::SwapBuffers() comment for details.
    context_provider_->ContextSupport()->Swap();
    cc::OutputSurface::PostSwapBuffersComplete();
  }
  cc::OverlayCandidateValidator* GetOverlayCandidateValidator() const override {
    return nullptr;
  }
  bool IsDisplayedAsOverlayPlane() const override { return false; }
  unsigned GetOverlayTextureId() const override { return 0; }
  bool SurfaceIsSuspendForRecycle() const override { return false; }
  uint32_t GetFramebufferCopyTextureFormat() override {
    // We assume we have an alpha channel from the BlimpContextProvider, so use
    // GL_RGBA here.
    return GL_RGBA;
  }
  bool HasExternalStencilTest() const override { return false; }
  void ApplyExternalStencil() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DisplayOutputSurface);
};

base::LazyInstance<SimpleTaskGraphRunner> g_task_graph_runner =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

BlimpEmbedderCompositor::BlimpEmbedderCompositor(
    CompositorDependencies* compositor_dependencies)
    : compositor_dependencies_(compositor_dependencies),
      frame_sink_id_(compositor_dependencies->AllocateFrameSinkId()),
      compositor_frame_sink_request_pending_(false),
      root_layer_(cc::Layer::Create()) {
  compositor_dependencies_->GetSurfaceManager()->RegisterFrameSinkId(
      frame_sink_id_);

  cc::LayerTreeHostInProcess::InitParams params;
  params.client = this;
  params.gpu_memory_buffer_manager =
      compositor_dependencies_->GetGpuMemoryBufferManager();
  params.task_graph_runner = g_task_graph_runner.Pointer();
  cc::LayerTreeSettings settings;
  params.settings = &settings;
  params.main_task_runner = base::ThreadTaskRunnerHandle::Get();
  params.animation_host = cc::AnimationHost::CreateMainInstance();
  host_ = cc::LayerTreeHostInProcess::CreateSingleThreaded(this, &params);

  root_layer_->SetBackgroundColor(SK_ColorWHITE);
  host_->GetLayerTree()->SetRootLayer(root_layer_);
  host_->SetFrameSinkId(frame_sink_id_);
  host_->SetVisible(true);
}

BlimpEmbedderCompositor::~BlimpEmbedderCompositor() {
  SetContextProvider(nullptr);
  compositor_dependencies_->GetSurfaceManager()->InvalidateFrameSinkId(
      frame_sink_id_);
}

void BlimpEmbedderCompositor::SetContentLayer(
    scoped_refptr<cc::Layer> content_layer) {
  root_layer_->RemoveAllChildren();
  root_layer_->AddChild(content_layer);
}

void BlimpEmbedderCompositor::SetSize(const gfx::Size& size_in_px) {
  viewport_size_in_px_ = size_in_px;

  // Update the host.
  host_->GetLayerTree()->SetViewportSize(viewport_size_in_px_);
  root_layer_->SetBounds(viewport_size_in_px_);

  // Update the display.
  if (display_) {
    display_->Resize(viewport_size_in_px_);
  }
}

void BlimpEmbedderCompositor::SetContextProvider(
    scoped_refptr<cc::ContextProvider> context_provider) {
  if (context_provider_) {
    DCHECK(host_->IsVisible());
    host_->SetVisible(false);
    host_->ReleaseCompositorFrameSink();
    display_.reset();
  }

  context_provider_ = std::move(context_provider);

  if (context_provider_) {
    host_->SetVisible(true);
    if (compositor_frame_sink_request_pending_) {
      HandlePendingCompositorFrameSinkRequest();
    }
  }
}

void BlimpEmbedderCompositor::RequestNewCompositorFrameSink() {
  DCHECK(!compositor_frame_sink_request_pending_)
      << "We already have a pending request?";
  compositor_frame_sink_request_pending_ = true;
  HandlePendingCompositorFrameSinkRequest();
}

void BlimpEmbedderCompositor::DidInitializeCompositorFrameSink() {
  compositor_frame_sink_request_pending_ = false;
}

void BlimpEmbedderCompositor::DidFailToInitializeCompositorFrameSink() {
  NOTREACHED() << "Can't fail to initialize the CompositorFrameSink here";
}

void BlimpEmbedderCompositor::HandlePendingCompositorFrameSinkRequest() {
  DCHECK(compositor_frame_sink_request_pending_);

  // Can't handle the request right now since we don't have a widget.
  if (!host_->IsVisible())
    return;

  DCHECK(context_provider_);

  gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager =
      compositor_dependencies_->GetGpuMemoryBufferManager();

  auto display_output_surface =
      base::MakeUnique<DisplayOutputSurface>(context_provider_);

  auto* task_runner = base::ThreadTaskRunnerHandle::Get().get();
  std::unique_ptr<cc::SyntheticBeginFrameSource> begin_frame_source(
      new cc::DelayBasedBeginFrameSource(
          base::MakeUnique<cc::DelayBasedTimeSource>(task_runner)));
  std::unique_ptr<cc::DisplayScheduler> scheduler(new cc::DisplayScheduler(
      begin_frame_source.get(), task_runner,
      display_output_surface->capabilities().max_frames_pending));

  display_ = base::MakeUnique<cc::Display>(
      nullptr /*shared_bitmap_manager*/, gpu_memory_buffer_manager,
      host_->GetSettings().renderer_settings, std::move(begin_frame_source),
      std::move(display_output_surface), std::move(scheduler),
      base::MakeUnique<cc::TextureMailboxDeleter>(task_runner));
  display_->SetVisible(true);
  display_->Resize(viewport_size_in_px_);

  // The Browser compositor and display share the same context provider.
  auto compositor_frame_sink = base::MakeUnique<cc::DirectCompositorFrameSink>(
      frame_sink_id_, compositor_dependencies_->GetSurfaceManager(),
      display_.get(), context_provider_, nullptr);

  host_->SetCompositorFrameSink(std::move(compositor_frame_sink));
}

}  // namespace client
}  // namespace blimp
