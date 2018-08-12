// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/surfaces/direct_compositor_frame_sink.h"

#include "base/bind.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/compositor_frame_sink_client.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/frame_sink_id.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"

namespace cc {

DirectCompositorFrameSink::DirectCompositorFrameSink(
    const FrameSinkId& frame_sink_id,
    SurfaceManager* surface_manager,
    Display* display,
    scoped_refptr<ContextProvider> context_provider,
    scoped_refptr<ContextProvider> worker_context_provider)
    : CompositorFrameSink(std::move(context_provider),
                          std::move(worker_context_provider)),
      frame_sink_id_(frame_sink_id),
      surface_manager_(surface_manager),
      display_(display),
      factory_(frame_sink_id, surface_manager, this) {
  DCHECK(thread_checker_.CalledOnValidThread());
  capabilities_.can_force_reclaim_resources = true;
  // Display and DirectCompositorFrameSink share a GL context, so sync
  // points aren't needed when passing resources between them.
  capabilities_.delegated_sync_points_required = false;
  factory_.set_needs_sync_points(false);
}

DirectCompositorFrameSink::DirectCompositorFrameSink(
    const FrameSinkId& frame_sink_id,
    SurfaceManager* surface_manager,
    Display* display,
    scoped_refptr<VulkanContextProvider> vulkan_context_provider)
    : CompositorFrameSink(std::move(vulkan_context_provider)),
      frame_sink_id_(frame_sink_id),
      surface_manager_(surface_manager),
      display_(display),
      factory_(frame_sink_id_, surface_manager, this) {
  DCHECK(thread_checker_.CalledOnValidThread());
  capabilities_.can_force_reclaim_resources = true;
}

DirectCompositorFrameSink::~DirectCompositorFrameSink() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (HasClient())
    DetachFromClient();
}

bool DirectCompositorFrameSink::BindToClient(
    CompositorFrameSinkClient* client) {
  DCHECK(thread_checker_.CalledOnValidThread());

  surface_manager_->RegisterSurfaceFactoryClient(frame_sink_id_, this);

  if (!CompositorFrameSink::BindToClient(client))
    return false;

  // We want the Display's output surface to hear about lost context, and since
  // this shares a context with it, we should not be listening for lost context
  // callbacks on the context here.
  if (context_provider())
    context_provider()->SetLostContextCallback(base::Closure());

  // Avoid initializing GL context here, as this should be sharing the
  // Display's context.
  display_->Initialize(this, surface_manager_, frame_sink_id_);
  return true;
}

void DirectCompositorFrameSink::DetachFromClient() {
  DCHECK(HasClient());
  // Unregister the SurfaceFactoryClient here instead of the dtor so that only
  // one client is alive for this namespace at any given time.
  surface_manager_->UnregisterSurfaceFactoryClient(frame_sink_id_);
  if (!delegated_local_frame_id_.is_null())
    factory_.Destroy(delegated_local_frame_id_);

  CompositorFrameSink::DetachFromClient();
}

void DirectCompositorFrameSink::SwapBuffers(CompositorFrame frame) {
  gfx::Size frame_size =
      frame.delegated_frame_data->render_pass_list.back()->output_rect.size();
  if (frame_size.IsEmpty() || frame_size != last_swap_frame_size_) {
    if (!delegated_local_frame_id_.is_null()) {
      factory_.Destroy(delegated_local_frame_id_);
    }
    delegated_local_frame_id_ = surface_id_allocator_.GenerateId();
    factory_.Create(delegated_local_frame_id_);
    last_swap_frame_size_ = frame_size;
  }
  display_->SetSurfaceId(SurfaceId(frame_sink_id_, delegated_local_frame_id_),
                         frame.metadata.device_scale_factor);

  factory_.SubmitCompositorFrame(
      delegated_local_frame_id_, std::move(frame),
      base::Bind(&DirectCompositorFrameSink::DidDrawCallback,
                 base::Unretained(this)));
}

void DirectCompositorFrameSink::ForceReclaimResources() {
  if (!delegated_local_frame_id_.is_null()) {
    factory_.SubmitCompositorFrame(delegated_local_frame_id_, CompositorFrame(),
                                   SurfaceFactory::DrawCallback());
  }
}

void DirectCompositorFrameSink::ReturnResources(
    const ReturnedResourceArray& resources) {
  if (client_)
    client_->ReclaimResources(resources);
}

void DirectCompositorFrameSink::SetBeginFrameSource(
    BeginFrameSource* begin_frame_source) {
  DCHECK(client_);
  client_->SetBeginFrameSource(begin_frame_source);
}

void DirectCompositorFrameSink::DisplayOutputSurfaceLost() {
  is_lost_ = true;
  client_->DidLoseCompositorFrameSink();
}

void DirectCompositorFrameSink::DisplayWillDrawAndSwap(
    bool will_draw_and_swap,
    const RenderPassList& render_passes) {
  // This notification is not relevant to our client outside of tests.
}

void DirectCompositorFrameSink::DisplayDidDrawAndSwap() {
  // This notification is not relevant to our client outside of tests. We
  // unblock the client from DidDrawCallback() when the surface is going to
  // be drawn.
}

void DirectCompositorFrameSink::DidDrawCallback() {
  // TODO(danakj): Why the lost check?
  if (!is_lost_)
    client_->DidSwapBuffersComplete();
}

}  // namespace cc
