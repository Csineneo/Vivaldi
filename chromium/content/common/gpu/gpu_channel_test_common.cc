// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/gpu_channel_test_common.h"

#include "base/test/test_simple_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "content/common/gpu/gpu_channel_manager_delegate.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "ipc/ipc_test_sink.h"

namespace content {

TestGpuChannelManagerDelegate::TestGpuChannelManagerDelegate() {}

TestGpuChannelManagerDelegate::~TestGpuChannelManagerDelegate() {}

void TestGpuChannelManagerDelegate::AddSubscription(int32_t client_id,
                                                    unsigned int target) {}

void TestGpuChannelManagerDelegate::ChannelEstablished(
    const IPC::ChannelHandle& channel_handle) {
  last_established_channel_handle_ = channel_handle;
}

void TestGpuChannelManagerDelegate::DidCreateOffscreenContext(
    const GURL& active_url) {}

void TestGpuChannelManagerDelegate::DidDestroyChannel(int client_id) {}

void TestGpuChannelManagerDelegate::DidDestroyOffscreenContext(
    const GURL& active_url) {}

void TestGpuChannelManagerDelegate::DidLoseContext(
    bool offscreen,
    gpu::error::ContextLostReason reason,
    const GURL& active_url) {}

void TestGpuChannelManagerDelegate::GpuMemoryUmaStats(
    const GPUMemoryUmaStats& params) {}

void TestGpuChannelManagerDelegate::RemoveSubscription(int32_t client_id,
                                                       unsigned int target) {}

void TestGpuChannelManagerDelegate::StoreShaderToDisk(
    int32_t client_id,
    const std::string& key,
    const std::string& shader) {}

#if defined(OS_MACOSX)
void TestGpuChannelManagerDelegate::SendAcceleratedSurfaceBuffersSwapped(
    const AcceleratedSurfaceBuffersSwappedParams& params) {}
#endif

#if defined(OS_WIN)
void TestGpuChannelManagerDelegate::SendAcceleratedSurfaceCreatedChildWindow(
    const gfx::PluginWindowHandle& parent_window,
    const gfx::PluginWindowHandle& child_window) {}
#endif

TestGpuChannelManager::TestGpuChannelManager(
    GpuChannelManagerDelegate* delegate,
    base::SingleThreadTaskRunner* task_runner,
    base::SingleThreadTaskRunner* io_task_runner,
    gpu::SyncPointManager* sync_point_manager,
    GpuMemoryBufferFactory* gpu_memory_buffer_factory)
    : GpuChannelManager(delegate,
                        nullptr,
                        task_runner,
                        io_task_runner,
                        nullptr,
                        sync_point_manager,
                        gpu_memory_buffer_factory) {}

TestGpuChannelManager::~TestGpuChannelManager() {
  // Clear gpu channels here so that any IPC messages sent are handled using the
  // overridden Send method.
  gpu_channels_.clear();
}

scoped_ptr<GpuChannel> TestGpuChannelManager::CreateGpuChannel(
    int client_id,
    uint64_t client_tracing_id,
    bool preempts,
    bool allow_view_command_buffers,
    bool allow_real_time_streams) {
  return make_scoped_ptr(new TestGpuChannel(
      this, sync_point_manager(), share_group(), mailbox_manager(),
      preempts ? preemption_flag() : nullptr,
      preempts ? nullptr : preemption_flag(), task_runner_.get(),
      io_task_runner_.get(), client_id, client_tracing_id,
      allow_view_command_buffers, allow_real_time_streams));
}

TestGpuChannel::TestGpuChannel(GpuChannelManager* gpu_channel_manager,
                               gpu::SyncPointManager* sync_point_manager,
                               gfx::GLShareGroup* share_group,
                               gpu::gles2::MailboxManager* mailbox_manager,
                               gpu::PreemptionFlag* preempting_flag,
                               gpu::PreemptionFlag* preempted_flag,
                               base::SingleThreadTaskRunner* task_runner,
                               base::SingleThreadTaskRunner* io_task_runner,
                               int client_id,
                               uint64_t client_tracing_id,
                               bool allow_view_command_buffers,
                               bool allow_real_time_streams)
    : GpuChannel(gpu_channel_manager,
                 sync_point_manager,
                 nullptr,
                 share_group,
                 mailbox_manager,
                 preempting_flag,
                 preempted_flag,
                 task_runner,
                 io_task_runner,
                 client_id,
                 client_tracing_id,
                 allow_view_command_buffers,
                 allow_real_time_streams) {}

TestGpuChannel::~TestGpuChannel() {
  // Call stubs here so that any IPC messages sent are handled using the
  // overridden Send method.
  stubs_.clear();
}

base::ProcessId TestGpuChannel::GetClientPID() const {
  return base::kNullProcessId;
}

IPC::ChannelHandle TestGpuChannel::Init(base::WaitableEvent* shutdown_event) {
  filter_->OnFilterAdded(&sink_);
  return IPC::ChannelHandle(channel_id());
}

bool TestGpuChannel::Send(IPC::Message* msg) {
  DCHECK(!msg->is_sync());
  return sink_.Send(msg);
}

// TODO(sunnyps): Use a mock memory buffer factory when necessary.
GpuChannelTestCommon::GpuChannelTestCommon()
    : task_runner_(new base::TestSimpleTaskRunner),
      io_task_runner_(new base::TestSimpleTaskRunner),
      sync_point_manager_(new gpu::SyncPointManager(false)),
      channel_manager_delegate_(new TestGpuChannelManagerDelegate()),
      channel_manager_(
          new TestGpuChannelManager(channel_manager_delegate_.get(),
                                    task_runner_.get(),
                                    io_task_runner_.get(),
                                    sync_point_manager_.get(),
                                    nullptr)) {}

GpuChannelTestCommon::~GpuChannelTestCommon() {
  // Destroying channels causes tasks to run on the IO task runner.
  channel_manager_ = nullptr;
  // Clear pending tasks to avoid refptr cycles that get flagged by ASAN.
  task_runner_->ClearPendingTasks();
  io_task_runner_->ClearPendingTasks();
}

}  // namespace content
