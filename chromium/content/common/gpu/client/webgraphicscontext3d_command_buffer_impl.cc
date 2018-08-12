// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/client/webgraphicscontext3d_command_buffer_impl.h"

#include "third_party/khronos/GLES2/gl2.h"
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "third_party/khronos/GLES2/gl2ext.h"

#include <algorithm>
#include <map>

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/profiler/scoped_tracker.h"
#include "base/trace_event/trace_event.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/gles2_cmd_helper.h"
#include "gpu/command_buffer/client/gles2_implementation.h"
#include "gpu/command_buffer/client/gles2_trace_implementation.h"
#include "gpu/command_buffer/client/gpu_switches.h"
#include "gpu/command_buffer/client/transfer_buffer.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/gpu_memory_allocation.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/skia_bindings/gl_bindings_skia_cmd_buffer.h"
#include "third_party/skia/include/core/SkTypes.h"

namespace content {

namespace {

static base::LazyInstance<base::Lock>::Leaky
    g_default_share_groups_lock = LAZY_INSTANCE_INITIALIZER;

typedef std::map<
    gpu::GpuChannelHost*,
    scoped_refptr<WebGraphicsContext3DCommandBufferImpl::ShareGroup>>
    ShareGroupMap;
static base::LazyInstance<ShareGroupMap> g_default_share_groups =
    LAZY_INSTANCE_INITIALIZER;

scoped_refptr<WebGraphicsContext3DCommandBufferImpl::ShareGroup>
GetDefaultShareGroupForHost(gpu::GpuChannelHost* host) {
  base::AutoLock lock(g_default_share_groups_lock.Get());

  ShareGroupMap& share_groups = g_default_share_groups.Get();
  ShareGroupMap::iterator it = share_groups.find(host);
  if (it == share_groups.end()) {
    scoped_refptr<WebGraphicsContext3DCommandBufferImpl::ShareGroup> group =
        new WebGraphicsContext3DCommandBufferImpl::ShareGroup();
    share_groups[host] = group;
    return group;
  }
  return it->second;
}

} // namespace anonymous

WebGraphicsContext3DCommandBufferImpl::SharedMemoryLimits::SharedMemoryLimits()
    : command_buffer_size(kDefaultCommandBufferSize),
      start_transfer_buffer_size(kDefaultStartTransferBufferSize),
      min_transfer_buffer_size(kDefaultMinTransferBufferSize),
      max_transfer_buffer_size(kDefaultMaxTransferBufferSize),
      mapped_memory_reclaim_limit(gpu::gles2::GLES2Implementation::kNoLimit) {}

WebGraphicsContext3DCommandBufferImpl::ShareGroup::ShareGroup() {
}

WebGraphicsContext3DCommandBufferImpl::ShareGroup::~ShareGroup() {
  DCHECK(contexts_.empty());
}

WebGraphicsContext3DCommandBufferImpl::WebGraphicsContext3DCommandBufferImpl(
    gpu::SurfaceHandle surface_handle,
    const GURL& active_url,
    gpu::GpuChannelHost* host,
    const gpu::gles2::ContextCreationAttribHelper& attributes,
    gfx::GpuPreference gpu_preference,
    bool share_resources,
    bool automatic_flushes,
    const SharedMemoryLimits& limits,
    WebGraphicsContext3DCommandBufferImpl* share_context)
    : automatic_flushes_(automatic_flushes),
      attributes_(attributes),
      host_(host),
      surface_handle_(surface_handle),
      active_url_(active_url),
      gpu_preference_(gpu_preference),
      mem_limits_(limits),
      weak_ptr_factory_(this) {
  switch (attributes.context_type) {
    case gpu::gles2::CONTEXT_TYPE_OPENGLES2:
    case gpu::gles2::CONTEXT_TYPE_OPENGLES3:
      context_type_ = CONTEXT_TYPE_UNKNOWN;
    case gpu::gles2::CONTEXT_TYPE_WEBGL1:
    case gpu::gles2::CONTEXT_TYPE_WEBGL2:
      context_type_ = OFFSCREEN_CONTEXT_FOR_WEBGL;
  }
  if (share_context) {
    DCHECK(!share_resources);
    share_group_ = share_context->share_group_;
  } else if (share_resources) {
    share_group_ = GetDefaultShareGroupForHost(host);
  } else {
    share_group_ = make_scoped_refptr(new ShareGroup);
  }
}

WebGraphicsContext3DCommandBufferImpl::
    ~WebGraphicsContext3DCommandBufferImpl() {
  if (real_gl_) {
    real_gl_->SetErrorMessageCallback(NULL);
  }

  Destroy();
}

bool WebGraphicsContext3DCommandBufferImpl::MaybeInitializeGL() {
  if (initialized_)
    return true;

  if (initialize_failed_)
    return false;

  TRACE_EVENT0("gpu", "WebGfxCtx3DCmdBfrImpl::MaybeInitializeGL");

  // TODO(vadimt): Remove ScopedTracker below once crbug.com/125248 is fixed.
  tracked_objects::ScopedTracker tracking_profile(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "125248 WebGraphicsContext3DCommandBufferImpl::MaybeInitializeGL"));

  if (!CreateContext()) {
    Destroy();

    initialize_failed_ = true;
    return false;
  }

  command_buffer_->SetContextLostCallback(
      base::Bind(&WebGraphicsContext3DCommandBufferImpl::OnContextLost,
                 weak_ptr_factory_.GetWeakPtr()));

  command_buffer_->SetOnConsoleMessageCallback(
      base::Bind(&WebGraphicsContext3DCommandBufferImpl::OnErrorMessage,
                 weak_ptr_factory_.GetWeakPtr()));

  real_gl_->SetErrorMessageCallback(getErrorMessageCallback());
  real_gl_->TraceBeginCHROMIUM("WebGraphicsContext3D",
                               "CommandBufferContext");

  initialized_ = true;
  return true;
}

bool WebGraphicsContext3DCommandBufferImpl::InitializeCommandBuffer(
    WebGraphicsContext3DCommandBufferImpl* share_context) {
  if (!host_.get())
    return false;

  gpu::CommandBufferProxyImpl* share_group_command_buffer = NULL;

  if (share_context) {
    share_group_command_buffer = share_context->GetCommandBufferProxy();
  }

  DCHECK(attributes_.buffer_preserved);
  std::vector<int32_t> serialized_attributes;
  attributes_.Serialize(&serialized_attributes);

  // Create a proxy to a command buffer in the GPU process.
  command_buffer_ = host_->CreateCommandBuffer(
      surface_handle_, gfx::Size(), share_group_command_buffer,
      gpu::GpuChannelHost::kDefaultStreamId,
      gpu::GpuChannelHost::kDefaultStreamPriority, serialized_attributes,
      active_url_, gpu_preference_);

  if (!command_buffer_) {
    DLOG(ERROR) << "GpuChannelHost failed to create command buffer.";
    UmaRecordContextInitFailed(context_type_);
    return false;
  }

  DVLOG_IF(1, gpu::error::IsError(command_buffer_->GetLastError()))
      << "Context dead on arrival. Last error: "
      << command_buffer_->GetLastError();
  // Initialize the command buffer.
  bool result = command_buffer_->Initialize();
  LOG_IF(ERROR, !result) << "CommandBufferProxy::Initialize failed.";
  if (!result)
    UmaRecordContextInitFailed(context_type_);
  return result;
}

bool WebGraphicsContext3DCommandBufferImpl::CreateContext() {
  TRACE_EVENT0("gpu", "WebGfxCtx3DCmdBfrImpl::CreateContext");
  scoped_refptr<gpu::gles2::ShareGroup> gles2_share_group;

  scoped_ptr<base::AutoLock> share_group_lock;
  bool add_to_share_group = false;
  if (!command_buffer_) {
    WebGraphicsContext3DCommandBufferImpl* share_context = NULL;

    share_group_lock.reset(new base::AutoLock(share_group_->lock()));
    share_context = share_group_->GetAnyContextLocked();

    if (!InitializeCommandBuffer(share_context)) {
      LOG(ERROR) << "Failed to initialize command buffer.";
      return false;
    }

    if (share_context)
      gles2_share_group = share_context->GetImplementation()->share_group();

    add_to_share_group = true;
  }

  // Create the GLES2 helper, which writes the command buffer protocol.
  gles2_helper_.reset(new gpu::gles2::GLES2CmdHelper(command_buffer_.get()));
  if (!gles2_helper_->Initialize(mem_limits_.command_buffer_size)) {
    LOG(ERROR) << "Failed to initialize GLES2CmdHelper.";
    return false;
  }

  if (!automatic_flushes_)
    gles2_helper_->SetAutomaticFlushes(false);
  // Create a transfer buffer used to copy resources between the renderer
  // process and the GPU process.
  transfer_buffer_ .reset(new gpu::TransferBuffer(gles2_helper_.get()));

  DCHECK(host_.get());

  const bool bind_generates_resource = attributes_.bind_generates_resource;
  const bool lose_context_when_out_of_memory =
      attributes_.lose_context_when_out_of_memory;
  const bool support_client_side_arrays = false;

  // Create the object exposing the OpenGL API.
  real_gl_.reset(new gpu::gles2::GLES2Implementation(
      gles2_helper_.get(), gles2_share_group.get(), transfer_buffer_.get(),
      bind_generates_resource, lose_context_when_out_of_memory,
      support_client_side_arrays, command_buffer_.get()));
  SetGLInterface(real_gl_.get());

  if (!real_gl_->Initialize(
      mem_limits_.start_transfer_buffer_size,
      mem_limits_.min_transfer_buffer_size,
      mem_limits_.max_transfer_buffer_size,
      mem_limits_.mapped_memory_reclaim_limit)) {
    LOG(ERROR) << "Failed to initialize GLES2Implementation.";
    return false;
  }

  if (add_to_share_group)
    share_group_->AddContextLocked(this);

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableGpuClientTracing)) {
    trace_gl_.reset(new gpu::gles2::GLES2TraceImplementation(GetGLInterface()));
    SetGLInterface(trace_gl_.get());
  }
  return true;
}

bool WebGraphicsContext3DCommandBufferImpl::InitializeOnCurrentThread() {
  if (!MaybeInitializeGL()) {
    DLOG(ERROR) << "Failed to initialize context.";
    return false;
  }
  if (gpu::error::IsError(command_buffer_->GetLastError())) {
    LOG(ERROR) << "Context dead on arrival. Last error: "
               << command_buffer_->GetLastError();
    return false;
  }

  return true;
}

void WebGraphicsContext3DCommandBufferImpl::Destroy() {
  share_group_->RemoveContext(this);

  gpu::gles2::GLES2Interface* gl = GetGLInterface();
  if (gl) {
    // First flush the context to ensure that any pending frees of resources
    // are completed. Otherwise, if this context is part of a share group,
    // those resources might leak. Also, any remaining side effects of commands
    // issued on this context might not be visible to other contexts in the
    // share group.
    gl->Flush();
    SetGLInterface(nullptr);
  }

  trace_gl_.reset();
  real_gl_.reset();
  transfer_buffer_.reset();
  gles2_helper_.reset();
  real_gl_.reset();
  command_buffer_.reset();

  host_ = nullptr;
}

gpu::ContextSupport*
WebGraphicsContext3DCommandBufferImpl::GetContextSupport() {
  return real_gl_.get();
}

bool WebGraphicsContext3DCommandBufferImpl::IsCommandBufferContextLost() {
  // If the channel shut down unexpectedly, let that supersede the
  // command buffer's state.
  if (host_.get() && host_->IsLost())
    return true;
  gpu::CommandBuffer::State state = command_buffer_->GetLastState();
  return gpu::error::IsError(state.error);
}

// static
WebGraphicsContext3DCommandBufferImpl*
WebGraphicsContext3DCommandBufferImpl::CreateOffscreenContext(
    gpu::GpuChannelHost* host,
    const gpu::gles2::ContextCreationAttribHelper& attributes,
    gfx::GpuPreference gpu_preference,
    bool share_resources,
    bool automatic_flushes,
    const GURL& active_url,
    const SharedMemoryLimits& limits,
    WebGraphicsContext3DCommandBufferImpl* share_context) {
  if (!host)
    return NULL;

  if (share_context && share_context->IsCommandBufferContextLost())
    return NULL;

  return new WebGraphicsContext3DCommandBufferImpl(
      gpu::kNullSurfaceHandle, active_url, host, attributes, gpu_preference,
      share_resources, automatic_flushes, limits, share_context);
}

void WebGraphicsContext3DCommandBufferImpl::OnContextLost() {
  if (context_lost_callback_)
    context_lost_callback_->onContextLost();

  share_group_->RemoveAllContexts();

  DCHECK(host_.get());
  {
    base::AutoLock lock(g_default_share_groups_lock.Get());
    g_default_share_groups.Get().erase(host_.get());
  }

  gpu::CommandBuffer::State state = command_buffer_->GetLastState();
  UmaRecordContextLost(context_type_, state.error, state.context_lost_reason);
}

}  // namespace content
