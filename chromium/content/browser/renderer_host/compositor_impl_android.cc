// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/compositor_impl_android.h"

#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <stdint.h>

#include <utility>

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/cancelable_callback.h"
#include "base/command_line.h"
#include "base/containers/hash_tables.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/sys_info.h"
#include "base/threading/simple_thread.h"
#include "base/threading/thread.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_task_runner_handle.h"
#include "cc/base/switches.h"
#include "cc/input/input_handler.h"
#include "cc/layers/layer.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/context_provider.h"
#include "cc/output/output_surface.h"
#include "cc/output/output_surface_client.h"
#include "cc/output/vulkan_in_process_context_provider.h"
#include "cc/raster/single_thread_task_graph_runner.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/surfaces/onscreen_display_client.h"
#include "cc/surfaces/surface_display_output_surface.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_settings.h"
#include "components/display_compositor/compositor_overlay_candidate_validator_android.h"
#include "components/display_compositor/gl_helper.h"
#include "content/browser/android/child_process_launcher_android.h"
#include "content/browser/gpu/browser_gpu_channel_host_factory.h"
#include "content/browser/gpu/browser_gpu_memory_buffer_manager.h"
#include "content/browser/gpu/compositor_util.h"
#include "content/browser/gpu/gpu_surface_tracker.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/gpu/client/context_provider_command_buffer.h"
#include "content/common/gpu_process_launch_causes.h"
#include "content/common/host_shared_bitmap_manager.h"
#include "content/public/browser/android/compositor.h"
#include "content/public/browser/android/compositor_client.h"
#include "content/public/common/content_switches.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/ipc/client/command_buffer_proxy_impl.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/vulkan/vulkan_surface.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/skia/include/core/SkMallocPixelRef.h"
#include "ui/android/window_android.h"
#include "ui/gfx/android/device_display_info.h"
#include "ui/gfx/swap_result.h"

namespace gpu {
struct GpuProcessHostedCALayerTreeParamsMac;
}

namespace content {

namespace {

const unsigned int kMaxDisplaySwapBuffers = 1U;

class ExternalBeginFrameSource : public cc::BeginFrameSourceBase,
                                 public CompositorImpl::VSyncObserver {
 public:
  ExternalBeginFrameSource(CompositorImpl* compositor)
      : compositor_(compositor) {
    compositor_->AddObserver(this);
  }

  ~ExternalBeginFrameSource() override { compositor_->RemoveObserver(this); }

  // cc::BeginFrameSourceBase implementation:
  void AddObserver(cc::BeginFrameObserver* obs) override {
    cc::BeginFrameSourceBase::AddObserver(obs);
    DCHECK(needs_begin_frames());
    if (!last_begin_frame_args_.IsValid())
      return;

    // Send a MISSED begin frame if necessary.
    cc::BeginFrameArgs last_args = obs->LastUsedBeginFrameArgs();
    if (!last_args.IsValid() ||
        (last_begin_frame_args_.frame_time > last_args.frame_time)) {
      last_begin_frame_args_.type = cc::BeginFrameArgs::MISSED;
      // TODO(crbug.com/602485): A deadline doesn't make too much sense
      // for a missed BeginFrame (the intention rather is 'immediately'),
      // but currently the retro frame logic is very strict in discarding
      // BeginFrames.
      last_begin_frame_args_.deadline =
          base::TimeTicks::Now() + last_begin_frame_args_.interval;
      obs->OnBeginFrame(last_begin_frame_args_);
    }
  }

  void OnNeedsBeginFramesChanged(bool needs_begin_frames) override {
    TRACE_EVENT1("compositor", "OnNeedsBeginFramesChanged",
                 "needs_begin_frames", needs_begin_frames);
    compositor_->OnNeedsBeginFramesChange(needs_begin_frames);
  }

  // CompositorImpl::VSyncObserver implementation:
  void OnVSync(base::TimeTicks frame_time,
               base::TimeDelta vsync_period) override {
    base::TimeTicks deadline = std::max(base::TimeTicks::Now(), frame_time);
    last_begin_frame_args_ =
        cc::BeginFrameArgs::Create(BEGINFRAME_FROM_HERE, frame_time, deadline,
                                   vsync_period, cc::BeginFrameArgs::NORMAL);
    CallOnBeginFrame(last_begin_frame_args_);
  }

 private:
  CompositorImpl* compositor_;
  cc::BeginFrameArgs last_begin_frame_args_;
};

// Used to override capabilities_.adjust_deadline_for_parent to false
class OutputSurfaceWithoutParent : public cc::OutputSurface,
                                   public CompositorImpl::VSyncObserver {
 public:
  OutputSurfaceWithoutParent(
      CompositorImpl* compositor,
      const scoped_refptr<ContextProviderCommandBuffer>& context_provider,
      const base::Callback<void(gpu::Capabilities)>&
          populate_gpu_capabilities_callback,
      std::unique_ptr<ExternalBeginFrameSource> begin_frame_source)
      : cc::OutputSurface(context_provider),
        compositor_(compositor),
        populate_gpu_capabilities_callback_(populate_gpu_capabilities_callback),
        swap_buffers_completion_callback_(
            base::Bind(&OutputSurfaceWithoutParent::OnSwapBuffersCompleted,
                       base::Unretained(this))),
        overlay_candidate_validator_(
            new display_compositor::
                CompositorOverlayCandidateValidatorAndroid()),
        begin_frame_source_(std::move(begin_frame_source)) {
    capabilities_.adjust_deadline_for_parent = false;
    capabilities_.max_frames_pending = kMaxDisplaySwapBuffers;
  }

  ~OutputSurfaceWithoutParent() override { compositor_->RemoveObserver(this); }

  void SwapBuffers(cc::CompositorFrame* frame) override {
    GetCommandBufferProxy()->SetLatencyInfo(frame->metadata.latency_info);
    if (frame->gl_frame_data->sub_buffer_rect.IsEmpty()) {
      context_provider_->ContextSupport()->CommitOverlayPlanes();
    } else {
      DCHECK(frame->gl_frame_data->sub_buffer_rect ==
             gfx::Rect(frame->gl_frame_data->size));
      context_provider_->ContextSupport()->Swap();
    }
    client_->DidSwapBuffers();
  }

  bool BindToClient(cc::OutputSurfaceClient* client) override {
    if (!OutputSurface::BindToClient(client))
      return false;

    GetCommandBufferProxy()->SetSwapBuffersCompletionCallback(
        swap_buffers_completion_callback_.callback());

    populate_gpu_capabilities_callback_.Run(
        context_provider_->ContextCapabilities());
    compositor_->AddObserver(this);

    client->SetBeginFrameSource(begin_frame_source_.get());

    return true;
  }

  void DetachFromClient() override {
    client_->SetBeginFrameSource(nullptr);
    OutputSurface::DetachFromClient();
  }

  cc::OverlayCandidateValidator* GetOverlayCandidateValidator() const override {
    return overlay_candidate_validator_.get();
  }

 private:
  gpu::CommandBufferProxyImpl* GetCommandBufferProxy() {
    ContextProviderCommandBuffer* provider_command_buffer =
        static_cast<content::ContextProviderCommandBuffer*>(
            context_provider_.get());
    gpu::CommandBufferProxyImpl* command_buffer_proxy =
        provider_command_buffer->GetCommandBufferProxy();
    DCHECK(command_buffer_proxy);
    return command_buffer_proxy;
  }

  void OnSwapBuffersCompleted(
      const std::vector<ui::LatencyInfo>& latency_info,
      gfx::SwapResult result,
      const gpu::GpuProcessHostedCALayerTreeParamsMac* params_mac) {
    RenderWidgetHostImpl::CompositorFrameDrawn(latency_info);
    OutputSurface::OnSwapBuffersComplete();
  }

  void OnVSync(base::TimeTicks timebase, base::TimeDelta interval) override {
    client_->CommitVSyncParameters(timebase, interval);
  }

 private:
  CompositorImpl* compositor_;
  base::Callback<void(gpu::Capabilities)> populate_gpu_capabilities_callback_;
  base::CancelableCallback<void(
      const std::vector<ui::LatencyInfo>&,
      gfx::SwapResult,
      const gpu::GpuProcessHostedCALayerTreeParamsMac* params_mac)>
      swap_buffers_completion_callback_;
  std::unique_ptr<cc::OverlayCandidateValidator> overlay_candidate_validator_;
  std::unique_ptr<ExternalBeginFrameSource> begin_frame_source_;
};

#if defined(ENABLE_VULKAN)
class VulkanOutputSurface : public cc::OutputSurface {
 public:
  VulkanOutputSurface(
      scoped_refptr<cc::VulkanContextProvider> vulkan_context_provider,
      std::unique_ptr<ExternalBeginFrameSource> begin_frame_source)
      : OutputSurface(nullptr,
                      nullptr,
                      std::move(vulkan_context_provider),
                      nullptr),
        begin_frame_source_(std::move(begin_frame_source)) {}

  ~VulkanOutputSurface() override { Destroy(); }

  bool Initialize(gfx::AcceleratedWidget widget) {
    DCHECK(!surface_);
    std::unique_ptr<gpu::VulkanSurface> surface(
        gpu::VulkanSurface::CreateViewSurface(widget));
    if (!surface->Initialize(vulkan_context_provider()->GetDeviceQueue(),
                             gpu::VulkanSurface::DEFAULT_SURFACE_FORMAT)) {
      return false;
    }
    surface_ = std::move(surface);

    return true;
  }

  bool BindToClient(cc::OutputSurfaceClient* client) override {
    if (!OutputSurface::BindToClient(client))
      return false;
    client->SetBeginFrameSource(begin_frame_source_.get());
    return true;
  }

  void SwapBuffers(cc::CompositorFrame* frame) override {
    surface_->SwapBuffers();
    PostSwapBuffersComplete();
    client_->DidSwapBuffers();
  }

  void Destroy() {
    if (surface_) {
      surface_->Destroy();
      surface_.reset();
    }
  }

  void OnSwapBuffersCompleted(const std::vector<ui::LatencyInfo>& latency_info,
                              gfx::SwapResult result) {
    RenderWidgetHostImpl::CompositorFrameDrawn(latency_info);
    OutputSurface::OnSwapBuffersComplete();
  }

 private:
  std::unique_ptr<gpu::VulkanSurface> surface_;
  std::unique_ptr<ExternalBeginFrameSource> begin_frame_source_;

  DISALLOW_COPY_AND_ASSIGN(VulkanOutputSurface);
};
#endif

base::LazyInstance<scoped_refptr<cc::VulkanInProcessContextProvider>>
    g_shared_vulkan_context_provider_android_ = LAZY_INSTANCE_INITIALIZER;

static bool g_initialized = false;

base::LazyInstance<cc::SurfaceManager> g_surface_manager =
    LAZY_INSTANCE_INITIALIZER;

int g_surface_id_namespace = 0;

class SingleThreadTaskGraphRunner : public cc::SingleThreadTaskGraphRunner {
 public:
  SingleThreadTaskGraphRunner() {
    Start("CompositorTileWorker1", base::SimpleThread::Options());
  }

  ~SingleThreadTaskGraphRunner() override {
    Shutdown();
  }
};

base::LazyInstance<SingleThreadTaskGraphRunner> g_task_graph_runner =
    LAZY_INSTANCE_INITIALIZER;

}  // anonymous namespace

// static
Compositor* Compositor::Create(CompositorClient* client,
                               gfx::NativeWindow root_window) {
  return client ? new CompositorImpl(client, root_window) : NULL;
}

// static
void Compositor::Initialize() {
  DCHECK(!CompositorImpl::IsInitialized());
  g_initialized = true;
}

// static
bool CompositorImpl::IsInitialized() {
  return g_initialized;
}

// static
cc::SurfaceManager* CompositorImpl::GetSurfaceManager() {
  return g_surface_manager.Pointer();
}

// static
std::unique_ptr<cc::SurfaceIdAllocator>
CompositorImpl::CreateSurfaceIdAllocator() {
  std::unique_ptr<cc::SurfaceIdAllocator> allocator(
      new cc::SurfaceIdAllocator(++g_surface_id_namespace));
  cc::SurfaceManager* manager = GetSurfaceManager();
  DCHECK(manager);
  allocator->RegisterSurfaceIdNamespace(manager);
  return allocator;
}

// static
scoped_refptr<cc::VulkanInProcessContextProvider>
CompositorImpl::SharedVulkanContextProviderAndroid() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableVulkan)) {
    scoped_refptr<cc::VulkanInProcessContextProvider>* context_provider =
        g_shared_vulkan_context_provider_android_.Pointer();
    if (!*context_provider)
      *context_provider = cc::VulkanInProcessContextProvider::Create();
    return *context_provider;
  }
  return nullptr;
}

CompositorImpl::CompositorImpl(CompositorClient* client,
                               gfx::NativeWindow root_window)
    : root_layer_(cc::Layer::Create()),
      surface_id_allocator_(CreateSurfaceIdAllocator()),
      resource_manager_(root_window),
      has_transparent_background_(false),
      device_scale_factor_(1),
      window_(NULL),
      surface_handle_(gpu::kNullSurfaceHandle),
      client_(client),
      root_window_(root_window),
      needs_animate_(false),
      pending_swapbuffers_(0U),
      num_successive_context_creation_failures_(0),
      output_surface_request_pending_(false),
      needs_begin_frames_(false),
      weak_factory_(this) {
  DCHECK(client);
  DCHECK(root_window);
  root_window->AttachCompositor(this);
  CreateLayerTreeHost();
  resource_manager_.Init(host_.get());
}

CompositorImpl::~CompositorImpl() {
  root_window_->DetachCompositor();
  // Clean-up any surface references.
  SetSurface(NULL);
}

ui::UIResourceProvider& CompositorImpl::GetUIResourceProvider() {
  return *this;
}

ui::ResourceManager& CompositorImpl::GetResourceManager() {
  return resource_manager_;
}

void CompositorImpl::SetRootLayer(scoped_refptr<cc::Layer> root_layer) {
  if (subroot_layer_.get()) {
    subroot_layer_->RemoveFromParent();
    subroot_layer_ = NULL;
  }
  if (root_layer.get()) {
    subroot_layer_ = root_layer;
    root_layer_->AddChild(root_layer);
  }
}

void CompositorImpl::SetSurface(jobject surface) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> j_surface(env, surface);

  GpuSurfaceTracker* tracker = GpuSurfaceTracker::Get();

  if (window_) {
    // Shut down GL context before unregistering surface.
    SetVisible(false);
    tracker->RemoveSurface(surface_handle_);
    ANativeWindow_release(window_);
    window_ = NULL;
    UnregisterViewSurface(surface_handle_);
    surface_handle_ = gpu::kNullSurfaceHandle;
  }

  ANativeWindow* window = NULL;
  if (surface) {
    // Note: This ensures that any local references used by
    // ANativeWindow_fromSurface are released immediately. This is needed as a
    // workaround for https://code.google.com/p/android/issues/detail?id=68174
    base::android::ScopedJavaLocalFrame scoped_local_reference_frame(env);
    window = ANativeWindow_fromSurface(env, surface);
  }

  if (window) {
    window_ = window;
    ANativeWindow_acquire(window);
    surface_handle_ = tracker->AddSurfaceForNativeWidget(window);
    // Register first, SetVisible() might create an OutputSurface.
    RegisterViewSurface(surface_handle_, j_surface.obj());
    SetVisible(true);
    ANativeWindow_release(window);
  }
}

void CompositorImpl::CreateLayerTreeHost() {
  DCHECK(!host_);

  cc::LayerTreeSettings settings;
  settings.renderer_settings.refresh_rate = 60.0;
  settings.renderer_settings.allow_antialiasing = false;
  settings.renderer_settings.highp_threshold_min = 2048;
  settings.use_zero_copy = true;

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  settings.initial_debug_state.SetRecordRenderingStats(
      command_line->HasSwitch(cc::switches::kEnableGpuBenchmarking));
  settings.initial_debug_state.show_fps_counter =
      command_line->HasSwitch(cc::switches::kUIShowFPSCounter);
  settings.single_thread_proxy_scheduler = true;

  cc::LayerTreeHost::InitParams params;
  params.client = this;
  params.shared_bitmap_manager = HostSharedBitmapManager::current();
  params.gpu_memory_buffer_manager = BrowserGpuMemoryBufferManager::current();
  params.task_graph_runner = g_task_graph_runner.Pointer();
  params.main_task_runner = base::ThreadTaskRunnerHandle::Get();
  params.settings = &settings;
  host_ = cc::LayerTreeHost::CreateSingleThreaded(this, &params);
  DCHECK(!host_->visible());
  host_->SetRootLayer(root_layer_);
  host_->set_surface_id_namespace(surface_id_allocator_->id_namespace());
  host_->SetViewportSize(size_);
  host_->set_has_transparent_background(has_transparent_background_);
  host_->SetDeviceScaleFactor(device_scale_factor_);

  if (needs_animate_)
    host_->SetNeedsAnimate();
}

void CompositorImpl::SetVisible(bool visible) {
  TRACE_EVENT1("cc", "CompositorImpl::SetVisible", "visible", visible);
  if (!visible) {
    DCHECK(host_->visible());
    host_->SetVisible(false);
    if (!host_->output_surface_lost())
      host_->ReleaseOutputSurface();
    pending_swapbuffers_ = 0;
    establish_gpu_channel_timeout_.Stop();
    display_client_.reset();
  } else {
    host_->SetVisible(true);
    if (output_surface_request_pending_)
      RequestNewOutputSurface();
  }
}

void CompositorImpl::setDeviceScaleFactor(float factor) {
  device_scale_factor_ = factor;
  if (host_)
    host_->SetDeviceScaleFactor(factor);
}

void CompositorImpl::SetWindowBounds(const gfx::Size& size) {
  if (size_ == size)
    return;

  size_ = size;
  if (host_)
    host_->SetViewportSize(size);
  if (display_client_)
    display_client_->display()->Resize(size);
  root_layer_->SetBounds(size);
}

void CompositorImpl::SetHasTransparentBackground(bool flag) {
  has_transparent_background_ = flag;
  if (host_)
    host_->set_has_transparent_background(flag);
}

void CompositorImpl::SetNeedsComposite() {
  if (!host_->visible())
    return;
  TRACE_EVENT0("compositor", "Compositor::SetNeedsComposite");
  host_->SetNeedsAnimate();
}

void CompositorImpl::UpdateLayerTreeHost() {
  client_->UpdateLayerTreeHost();
  if (needs_animate_) {
    needs_animate_ = false;
    root_window_->Animate(base::TimeTicks::Now());
  }
}

void CompositorImpl::OnGpuChannelEstablished() {
  establish_gpu_channel_timeout_.Stop();
  CreateOutputSurface();
}

void CompositorImpl::OnGpuChannelTimeout() {
  LOG(FATAL) << "Timed out waiting for GPU channel.";
}

void CompositorImpl::RequestNewOutputSurface() {
  output_surface_request_pending_ = true;

#if defined(ADDRESS_SANITIZER) || defined(THREAD_SANITIZER) || \
  defined(SYZYASAN) || defined(CYGPROFILE_INSTRUMENTATION)
  const int64_t kGpuChannelTimeoutInSeconds = 40;
#else
  const int64_t kGpuChannelTimeoutInSeconds = 10;
#endif

  BrowserGpuChannelHostFactory* factory =
      BrowserGpuChannelHostFactory::instance();
  if (!factory->GetGpuChannel()) {
    factory->EstablishGpuChannel(
        CAUSE_FOR_GPU_LAUNCH_DISPLAY_COMPOSITOR_CONTEXT,
        base::Bind(&CompositorImpl::OnGpuChannelEstablished,
                   weak_factory_.GetWeakPtr()));
    establish_gpu_channel_timeout_.Start(
        FROM_HERE, base::TimeDelta::FromSeconds(kGpuChannelTimeoutInSeconds),
        this, &CompositorImpl::OnGpuChannelTimeout);
    return;
  }

  CreateOutputSurface();
}

void CompositorImpl::DidInitializeOutputSurface() {
  num_successive_context_creation_failures_ = 0;
  output_surface_request_pending_ = false;
}

void CompositorImpl::DidFailToInitializeOutputSurface() {
  LOG(ERROR) << "Failed to init OutputSurface for compositor.";
  LOG_IF(FATAL, ++num_successive_context_creation_failures_ >= 2)
      << "Too many context creation failures. Giving up... ";
  RequestNewOutputSurface();
}

void CompositorImpl::CreateOutputSurface() {
  // We might have had a request from a LayerTreeHost that was then
  // hidden (and hidden means we don't have a native surface).
  // Also make sure we only handle this once.
  if (!output_surface_request_pending_ || !host_->visible())
    return;

  scoped_refptr<ContextProviderCommandBuffer> context_provider;
  scoped_refptr<cc::VulkanInProcessContextProvider> vulkan_context_provider =
      SharedVulkanContextProviderAndroid();
  std::unique_ptr<cc::OutputSurface> real_output_surface;
#if defined(ENABLE_VULKAN)
  std::unique_ptr<VulkanOutputSurface> vulkan_surface;
  if (vulkan_context_provider) {
    vulkan_surface.reset(new VulkanOutputSurface(
        std::move(vulkan_context_provider),
        base::WrapUnique(new ExternalBeginFrameSource(this))));
    if (!vulkan_surface->Initialize(window_)) {
      vulkan_surface->Destroy();
      vulkan_surface.reset();
    } else {
      real_output_surface = std::move(vulkan_surface);
    }
  }
#endif

  if (!real_output_surface) {
    // This is used for the browser compositor (offscreen) and for the display
    // compositor (onscreen), so ask for capabilities needed by either one.
    // The default framebuffer for an offscreen context is not used, so it does
    // not need alpha, stencil, depth, antialiasing. The display compositor does
    // not use these things either, except for alpha when it has a transparent
    // background.
    gpu::gles2::ContextCreationAttribHelper attributes;
    attributes.alpha_size = -1;
    attributes.stencil_size = 0;
    attributes.depth_size = 0;
    attributes.samples = 0;
    attributes.sample_buffers = 0;
    attributes.bind_generates_resource = false;

    if (has_transparent_background_) {
      attributes.alpha_size = 8;
    } else if (base::SysInfo::IsLowEndDevice()) {
      // In this case we prefer to use RGB565 format instead of RGBA8888 if
      // possible.
      // TODO(danakj): GpuCommandBufferStub constructor checks for alpha == 0 in
      // order to enable 565, but it should avoid using 565 when -1s are
      // specified
      // (IOW check that a <= 0 && rgb > 0 && rgb <= 565) then alpha should be
      // -1.
      attributes.alpha_size = 0;
      attributes.red_size = 5;
      attributes.green_size = 6;
      attributes.blue_size = 5;
    }

    pending_swapbuffers_ = 0;

    DCHECK(window_);
    DCHECK_NE(surface_handle_, gpu::kNullSurfaceHandle);

    BrowserGpuChannelHostFactory* factory =
        BrowserGpuChannelHostFactory::instance();
    scoped_refptr<gpu::GpuChannelHost> gpu_channel_host(
        factory->GetGpuChannel());
    // If the channel was already lost, we'll get null back here and need to
    // try again.
    if (!gpu_channel_host) {
      RequestNewOutputSurface();
      return;
    }

    GURL url("chrome://gpu/CompositorImpl::CreateOutputSurface");
    constexpr bool automatic_flushes = false;
    constexpr bool support_locking = false;

    constexpr size_t kBytesPerPixel = 4;
    const size_t full_screen_texture_size_in_bytes =
        gfx::DeviceDisplayInfo().GetDisplayHeight() *
        gfx::DeviceDisplayInfo().GetDisplayWidth() * kBytesPerPixel;

    gpu::SharedMemoryLimits limits;
    // This limit is meant to hold the contents of the display compositor
    // drawing the scene. See discussion here:
    // https://codereview.chromium.org/1900993002/diff/90001/content/browser/renderer_host/compositor_impl_android.cc?context=3&column_width=80&tab_spaces=8
    limits.command_buffer_size = 64 * 1024;
    // These limits are meant to hold the uploads for the browser UI without
    // any excess space.
    limits.start_transfer_buffer_size = 64 * 1024;
    limits.min_transfer_buffer_size = 64 * 1024;
    limits.max_transfer_buffer_size = full_screen_texture_size_in_bytes;
    // Texture uploads may use mapped memory so give a reasonable limit for
    // them.
    limits.mapped_memory_reclaim_limit = full_screen_texture_size_in_bytes;

    context_provider = new ContextProviderCommandBuffer(
        std::move(gpu_channel_host), gpu::GPU_STREAM_DEFAULT,
        gpu::GpuStreamPriority::NORMAL, surface_handle_, url,
        gfx::PreferIntegratedGpu, automatic_flushes, support_locking, limits,
        attributes, nullptr,
        command_buffer_metrics::DISPLAY_COMPOSITOR_ONSCREEN_CONTEXT);
    DCHECK(context_provider.get());

    real_output_surface = base::WrapUnique(new OutputSurfaceWithoutParent(
        this, context_provider,
        base::Bind(&CompositorImpl::PopulateGpuCapabilities,
                   base::Unretained(this)),
        base::WrapUnique(new ExternalBeginFrameSource(this))));
  }

  cc::SurfaceManager* manager = GetSurfaceManager();
  display_client_.reset(new cc::OnscreenDisplayClient(
      std::move(real_output_surface), manager,
      HostSharedBitmapManager::current(),
      BrowserGpuMemoryBufferManager::current(),
      host_->settings().renderer_settings, base::ThreadTaskRunnerHandle::Get(),
      surface_id_allocator_->id_namespace()));

  std::unique_ptr<cc::SurfaceDisplayOutputSurface> surface_output_surface(
      vulkan_context_provider
          ? new cc::SurfaceDisplayOutputSurface(
                manager, surface_id_allocator_.get(),
                static_cast<scoped_refptr<cc::VulkanContextProvider>>(
                    vulkan_context_provider))
          : new cc::SurfaceDisplayOutputSurface(manager,
                                                surface_id_allocator_.get(),
                                                context_provider, nullptr));

  display_client_->set_surface_output_surface(surface_output_surface.get());
  surface_output_surface->set_display_client(display_client_.get());
  display_client_->display()->Resize(size_);
  host_->SetOutputSurface(std::move(surface_output_surface));
}

void CompositorImpl::PopulateGpuCapabilities(
    gpu::Capabilities gpu_capabilities) {
  gpu_capabilities_ = gpu_capabilities;
}

void CompositorImpl::AddObserver(VSyncObserver* observer) {
  observer_list_.AddObserver(observer);
}

void CompositorImpl::RemoveObserver(VSyncObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

cc::UIResourceId CompositorImpl::CreateUIResource(
    cc::UIResourceClient* client) {
  TRACE_EVENT0("compositor", "CompositorImpl::CreateUIResource");
  return host_->CreateUIResource(client);
}

void CompositorImpl::DeleteUIResource(cc::UIResourceId resource_id) {
  TRACE_EVENT0("compositor", "CompositorImpl::DeleteUIResource");
  host_->DeleteUIResource(resource_id);
}

bool CompositorImpl::SupportsETC1NonPowerOfTwo() const {
  return gpu_capabilities_.texture_format_etc1_npot;
}

void CompositorImpl::DidPostSwapBuffers() {
  TRACE_EVENT0("compositor", "CompositorImpl::DidPostSwapBuffers");
  pending_swapbuffers_++;
}

void CompositorImpl::DidCompleteSwapBuffers() {
  TRACE_EVENT0("compositor", "CompositorImpl::DidCompleteSwapBuffers");
  DCHECK_GT(pending_swapbuffers_, 0U);
  pending_swapbuffers_--;
  client_->OnSwapBuffersCompleted(pending_swapbuffers_);
}

void CompositorImpl::DidAbortSwapBuffers() {
  TRACE_EVENT0("compositor", "CompositorImpl::DidAbortSwapBuffers");
  // This really gets called only once from
  // SingleThreadProxy::DidLoseOutputSurfaceOnImplThread() when the
  // context was lost.
  if (host_->visible())
    host_->SetNeedsCommit();
  client_->OnSwapBuffersCompleted(0);
}

void CompositorImpl::DidCommit() {
  root_window_->OnCompositingDidCommit();
}

void CompositorImpl::RequestCopyOfOutputOnRootLayer(
    std::unique_ptr<cc::CopyOutputRequest> request) {
  root_layer_->RequestCopyOfOutput(std::move(request));
}

void CompositorImpl::OnVSync(base::TimeTicks frame_time,
                             base::TimeDelta vsync_period) {
  FOR_EACH_OBSERVER(VSyncObserver, observer_list_,
                    OnVSync(frame_time, vsync_period));
  if (needs_begin_frames_)
    root_window_->RequestVSyncUpdate();
}

void CompositorImpl::OnNeedsBeginFramesChange(bool needs_begin_frames) {
  if (needs_begin_frames_ == needs_begin_frames)
    return;

  needs_begin_frames_ = needs_begin_frames;
  if (needs_begin_frames_)
    root_window_->RequestVSyncUpdate();
}

void CompositorImpl::SetNeedsAnimate() {
  needs_animate_ = true;
  if (!host_->visible())
    return;

  TRACE_EVENT0("compositor", "Compositor::SetNeedsAnimate");
  host_->SetNeedsAnimate();
}

}  // namespace content
