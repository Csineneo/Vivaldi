// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/hardware_renderer.h"

#include <utility>

#include "android_webview/browser/aw_gl_surface.h"
#include "android_webview/browser/aw_render_thread_context_provider.h"
#include "android_webview/browser/child_frame.h"
#include "android_webview/browser/deferred_gpu_command_service.h"
#include "android_webview/browser/parent_compositor_draw_constraints.h"
#include "android_webview/browser/parent_output_surface.h"
#include "android_webview/browser/render_thread_manager.h"
#include "android_webview/public/browser/draw_gl.h"
#include "base/auto_reset.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/trace_event.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/output_surface.h"
#include "cc/output/renderer_settings.h"
#include "cc/quads/shared_quad_state.h"
#include "cc/quads/surface_draw_quad.h"
#include "cc/surfaces/display.h"
#include "cc/surfaces/surface_factory.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "gpu/command_buffer/client/gl_in_process_context.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/transform.h"
#include "ui/gl/gl_bindings.h"

namespace android_webview {

HardwareRenderer::HardwareRenderer(RenderThreadManager* state)
    : render_thread_manager_(state),
      last_egl_context_(eglGetCurrentContext()),
      gl_surface_(new AwGLSurface),
      compositor_id_(0u),  // Valid compositor id starts at 1.
      last_committed_output_surface_id_(0u),
      last_submitted_output_surface_id_(0u),
      output_surface_(NULL) {
  DCHECK(last_egl_context_);

  cc::RendererSettings settings;

  // Should be kept in sync with compositor_impl_android.cc.
  settings.allow_antialiasing = false;
  settings.highp_threshold_min = 2048;

  // Webview does not own the surface so should not clear it.
  settings.should_clear_root_render_pass = false;

  surface_manager_.reset(new cc::SurfaceManager);
  surface_id_allocator_.reset(new cc::SurfaceIdAllocator(1));
  surface_id_allocator_->RegisterSurfaceIdNamespace(surface_manager_.get());
  surface_manager_->RegisterSurfaceFactoryClient(
      surface_id_allocator_->id_namespace(), this);
  display_.reset(new cc::Display(this, surface_manager_.get(), nullptr, nullptr,
                                 settings,
                                 surface_id_allocator_->id_namespace()));
}

HardwareRenderer::~HardwareRenderer() {
  // Must reset everything before |surface_factory_| to ensure all
  // resources are returned before resetting.
  if (!root_id_.is_null())
    surface_factory_->Destroy(root_id_);
  if (!child_id_.is_null())
    surface_factory_->Destroy(child_id_);
  display_.reset();
  surface_factory_.reset();
  surface_manager_->UnregisterSurfaceFactoryClient(
      surface_id_allocator_->id_namespace());

  // Reset draw constraints.
  render_thread_manager_->PostExternalDrawConstraintsToChildCompositorOnRT(
      ParentCompositorDrawConstraints());
  ReturnResourcesInChildFrame();
}

void HardwareRenderer::CommitFrame() {
  TRACE_EVENT0("android_webview", "CommitFrame");
  scroll_offset_ = render_thread_manager_->GetScrollOffsetOnRT();
  std::unique_ptr<ChildFrame> child_frame =
      render_thread_manager_->PassFrameOnRT();
  if (!child_frame.get())
    return;

  last_committed_output_surface_id_ = child_frame->output_surface_id;
  ReturnResourcesInChildFrame();
  child_frame_ = std::move(child_frame);
  DCHECK(child_frame_->frame.get());
  DCHECK(!child_frame_->frame->gl_frame_data);
}

void HardwareRenderer::DrawGL(AwDrawGLInfo* draw_info,
                              const ScopedAppGLStateRestore& gl_state) {
  TRACE_EVENT0("android_webview", "HardwareRenderer::DrawGL");

  // We need to watch if the current Android context has changed and enforce
  // a clean-up in the compositor.
  EGLContext current_context = eglGetCurrentContext();
  DCHECK(current_context) << "DrawGL called without EGLContext";

  // TODO(boliu): Handle context loss.
  if (last_egl_context_ != current_context)
    DLOG(WARNING) << "EGLContextChanged";

  // SurfaceFactory::SubmitCompositorFrame might call glFlush. So calling it
  // during "kModeSync" stage (which does not allow GL) might result in extra
  // kModeProcess. Instead, submit the frame in "kModeDraw" stage to avoid
  // unnecessary kModeProcess.
  if (child_frame_.get() && child_frame_->frame.get()) {
    if (compositor_id_ != child_frame_->compositor_id ||
        last_submitted_output_surface_id_ != child_frame_->output_surface_id) {
      if (!root_id_.is_null())
        surface_factory_->Destroy(root_id_);
      if (!child_id_.is_null())
        surface_factory_->Destroy(child_id_);

      root_id_ = cc::SurfaceId();
      child_id_ = cc::SurfaceId();

      // This will return all the resources to the previous compositor.
      surface_factory_.reset();
      compositor_id_ = child_frame_->compositor_id;
      last_submitted_output_surface_id_ = child_frame_->output_surface_id;
      surface_factory_.reset(
          new cc::SurfaceFactory(surface_manager_.get(), this));
    }

    std::unique_ptr<cc::CompositorFrame> child_compositor_frame =
        std::move(child_frame_->frame);

    // On Android we put our browser layers in physical pixels and set our
    // browser CC device_scale_factor to 1, so suppress the transform between
    // DIP and pixels.
    child_compositor_frame->delegated_frame_data->device_scale_factor = 1.0f;

    gfx::Size frame_size =
        child_compositor_frame->delegated_frame_data->render_pass_list.back()
            ->output_rect.size();
    bool size_changed = frame_size != frame_size_;
    frame_size_ = frame_size;
    if (child_id_.is_null() || size_changed) {
      if (!child_id_.is_null())
        surface_factory_->Destroy(child_id_);
      child_id_ = surface_id_allocator_->GenerateId();
      surface_factory_->Create(child_id_);
    }

    surface_factory_->SubmitCompositorFrame(child_id_,
                                            std::move(child_compositor_frame),
                                            cc::SurfaceFactory::DrawCallback());
  }

  gfx::Transform transform(gfx::Transform::kSkipInitialization);
  transform.matrix().setColMajorf(draw_info->transform);
  transform.Translate(scroll_offset_.x(), scroll_offset_.y());

  gfx::Size viewport(draw_info->width, draw_info->height);
  // Need to post the new transform matrix back to child compositor
  // because there is no onDraw during a Render Thread animation, and child
  // compositor might not have the tiles rasterized as the animation goes on.
  ParentCompositorDrawConstraints draw_constraints(
      draw_info->is_layer, transform, viewport.IsEmpty());
  if (!child_frame_.get() || draw_constraints.NeedUpdate(*child_frame_)) {
    render_thread_manager_->PostExternalDrawConstraintsToChildCompositorOnRT(
        draw_constraints);
  }

  if (child_id_.is_null())
    return;

  gfx::Rect clip(draw_info->clip_left, draw_info->clip_top,
                 draw_info->clip_right - draw_info->clip_left,
                 draw_info->clip_bottom - draw_info->clip_top);

  // Create a frame with a single SurfaceDrawQuad referencing the child
  // Surface and transformed using the given transform.
  std::unique_ptr<cc::RenderPass> render_pass = cc::RenderPass::Create();
  render_pass->SetAll(cc::RenderPassId(1, 1), gfx::Rect(viewport), clip,
                      gfx::Transform(), false);

  cc::SharedQuadState* quad_state =
      render_pass->CreateAndAppendSharedQuadState();
  quad_state->quad_to_target_transform = transform;
  quad_state->quad_layer_bounds = frame_size_;
  quad_state->visible_quad_layer_rect = gfx::Rect(frame_size_);
  quad_state->opacity = 1.f;

  cc::SurfaceDrawQuad* surface_quad =
      render_pass->CreateAndAppendDrawQuad<cc::SurfaceDrawQuad>();
  surface_quad->SetNew(quad_state, gfx::Rect(quad_state->quad_layer_bounds),
                       gfx::Rect(quad_state->quad_layer_bounds), child_id_);

  std::unique_ptr<cc::DelegatedFrameData> delegated_frame(
      new cc::DelegatedFrameData);
  delegated_frame->render_pass_list.push_back(std::move(render_pass));
  std::unique_ptr<cc::CompositorFrame> frame(new cc::CompositorFrame);
  frame->delegated_frame_data = std::move(delegated_frame);

  if (root_id_.is_null()) {
    root_id_ = surface_id_allocator_->GenerateId();
    surface_factory_->Create(root_id_);
    display_->SetSurfaceId(root_id_, 1.f);
  }
  surface_factory_->SubmitCompositorFrame(root_id_, std::move(frame),
                                          cc::SurfaceFactory::DrawCallback());

  display_->Resize(viewport);

  if (!output_surface_) {
    scoped_refptr<cc::ContextProvider> context_provider =
        AwRenderThreadContextProvider::Create(
            gl_surface_, DeferredGpuCommandService::GetInstance());
    std::unique_ptr<ParentOutputSurface> output_surface_holder(
        new ParentOutputSurface(context_provider));
    output_surface_ = output_surface_holder.get();
    display_->Initialize(std::move(output_surface_holder), nullptr);
  }
  output_surface_->SetGLState(gl_state);
  display_->SetExternalClip(clip);
  display_->DrawAndSwap();
}

void HardwareRenderer::ReturnResources(
    const cc::ReturnedResourceArray& resources) {
  ReturnResourcesToCompositor(resources, compositor_id_,
                              last_submitted_output_surface_id_);
}

void HardwareRenderer::SetBeginFrameSource(
    cc::BeginFrameSource* begin_frame_source) {
  // TODO(tansell): Hook this up.
}

void HardwareRenderer::SetBackingFrameBufferObject(
    int framebuffer_binding_ext) {
  gl_surface_->SetBackingFrameBufferObject(framebuffer_binding_ext);
}

void HardwareRenderer::ReturnResourcesInChildFrame() {
  if (child_frame_.get() && child_frame_->frame.get()) {
    cc::ReturnedResourceArray resources_to_return;
    cc::TransferableResource::ReturnResources(
        child_frame_->frame->delegated_frame_data->resource_list,
        &resources_to_return);

    // The child frame's compositor id is not necessarily same as
    // compositor_id_.
    ReturnResourcesToCompositor(resources_to_return,
                                child_frame_->compositor_id,
                                child_frame_->output_surface_id);
  }
  child_frame_.reset();
}

void HardwareRenderer::ReturnResourcesToCompositor(
    const cc::ReturnedResourceArray& resources,
    uint32_t compositor_id,
    uint32_t output_surface_id) {
  if (output_surface_id != last_committed_output_surface_id_)
    return;
  render_thread_manager_->InsertReturnedResourcesOnRT(resources, compositor_id,
                                                      output_surface_id);
}

}  // namespace android_webview
