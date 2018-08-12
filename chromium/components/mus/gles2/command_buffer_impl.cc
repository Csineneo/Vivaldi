// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mus/gles2/command_buffer_impl.h"

#include "base/bind.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "components/mus/gles2/command_buffer_driver.h"
#include "components/mus/gles2/command_buffer_impl_observer.h"
#include "components/mus/gles2/command_buffer_type_conversions.h"
#include "components/mus/gles2/gpu_state.h"
#include "gpu/command_buffer/service/sync_point_manager.h"

namespace mus {

namespace {

uint64_t g_next_command_buffer_id = 0;

void RunInitializeCallback(
    const mojom::CommandBuffer::InitializeCallback& callback,
    mojom::CommandBufferInfoPtr info) {
  callback.Run(std::move(info));
}

void RunMakeProgressCallback(
    const mojom::CommandBuffer::MakeProgressCallback& callback,
    const gpu::CommandBuffer::State& state) {
  callback.Run(mojom::CommandBufferState::From(state));
}

}  // namespace

class CommandBufferImpl::CommandBufferDriverClientImpl
    : public CommandBufferDriver::Client {
 public:
  explicit CommandBufferDriverClientImpl(CommandBufferImpl* command_buffer)
      : command_buffer_(command_buffer) {}

 private:
  void DidLoseContext(uint32_t reason) override {
    command_buffer_->DidLoseContext(reason);
  }
  void UpdateVSyncParameters(int64_t timebase, int64_t interval) override {}

  CommandBufferImpl* command_buffer_;

  DISALLOW_COPY_AND_ASSIGN(CommandBufferDriverClientImpl);
};

CommandBufferImpl::CommandBufferImpl(
    mojo::InterfaceRequest<mus::mojom::CommandBuffer> request,
    scoped_refptr<GpuState> gpu_state)
    : gpu_state_(gpu_state), observer_(nullptr) {
  // Bind |CommandBufferImpl| to the |request| in the GPU control thread.
  gpu_state_->control_task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CommandBufferImpl::BindToRequest,
                 base::Unretained(this), base::Passed(&request)));
}

void CommandBufferImpl::DidLoseContext(uint32_t reason) {
  driver_->set_client(nullptr);
  loss_observer_->DidLoseContext(reason);
}

CommandBufferImpl::~CommandBufferImpl() {
  // Retire all sync points.
  for (uint32_t sync_point : sync_points_)
    gpu_state_->sync_point_manager()->RetireSyncPoint(sync_point);
  if (observer_)
    observer_->OnCommandBufferImplDestroyed();
}

void CommandBufferImpl::Initialize(
    mus::mojom::CommandBufferLostContextObserverPtr loss_observer,
    mojo::ScopedSharedBufferHandle shared_state,
    mojo::Array<int32_t> attribs,
    const mojom::CommandBuffer::InitializeCallback& callback) {
  gpu_state_->command_buffer_task_runner()->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CommandBufferImpl::InitializeOnGpuThread,
                 base::Unretained(this), base::Passed(&loss_observer),
                 base::Passed(&shared_state), base::Passed(&attribs),
                 base::Bind(&RunInitializeCallback, callback)));
}

void CommandBufferImpl::SetGetBuffer(int32_t buffer) {
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(), base::Bind(&CommandBufferImpl::SetGetBufferOnGpuThread,
                                base::Unretained(this), buffer));
}

void CommandBufferImpl::Flush(int32_t put_offset) {
  gpu::SyncPointManager* sync_point_manager = gpu_state_->sync_point_manager();
  const uint32_t order_num = driver_->sync_point_order_data()
      ->GenerateUnprocessedOrderNumber(sync_point_manager);
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(), base::Bind(&CommandBufferImpl::FlushOnGpuThread,
                                base::Unretained(this), put_offset, order_num));
}

void CommandBufferImpl::MakeProgress(
    int32_t last_get_offset,
    const mojom::CommandBuffer::MakeProgressCallback& callback) {
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(), base::Bind(&CommandBufferImpl::MakeProgressOnGpuThread,
                                base::Unretained(this), last_get_offset,
                                base::Bind(&RunMakeProgressCallback,
                                           callback)));
}

void CommandBufferImpl::RegisterTransferBuffer(
    int32_t id,
    mojo::ScopedSharedBufferHandle transfer_buffer,
    uint32_t size) {
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(),
      base::Bind(&CommandBufferImpl::RegisterTransferBufferOnGpuThread,
                 base::Unretained(this), id, base::Passed(&transfer_buffer),
                 size));
}

void CommandBufferImpl::DestroyTransferBuffer(int32_t id) {
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(),
      base::Bind(&CommandBufferImpl::DestroyTransferBufferOnGpuThread,
                 base::Unretained(this), id));
}

void CommandBufferImpl::InsertSyncPoint(
    bool retire,
    const mojom::CommandBuffer::InsertSyncPointCallback& callback) {
  uint32_t sync_point = gpu_state_->sync_point_manager()->GenerateSyncPoint();
  sync_points_.push_back(sync_point);
  callback.Run(sync_point);
  if (retire)
    RetireSyncPoint(sync_point);
}

void CommandBufferImpl::RetireSyncPoint(uint32_t sync_point) {
  DCHECK(!sync_points_.empty() && sync_points_.front() == sync_point);
  sync_points_.pop_front();
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(), base::Bind(&CommandBufferImpl::RetireSyncPointOnGpuThread,
                                base::Unretained(this), sync_point));
}

void CommandBufferImpl::CreateImage(int32_t id,
                                    mojo::ScopedHandle memory_handle,
                                    int32_t type,
                                    mojo::SizePtr size,
                                    int32_t format,
                                    int32_t internal_format) {
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(),
      base::Bind(&CommandBufferImpl::CreateImageOnGpuThread,
                 base::Unretained(this), id, base::Passed(&memory_handle), type,
                 base::Passed(&size), format, internal_format));
}

void CommandBufferImpl::DestroyImage(int32_t id) {
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(), base::Bind(&CommandBufferImpl::DestroyImageOnGpuThread,
                                base::Unretained(this), id));
}

void CommandBufferImpl::BindToRequest(
    mojo::InterfaceRequest<mus::mojom::CommandBuffer> request) {
  binding_.reset(
      new mojo::Binding<mus::mojom::CommandBuffer>(this, std::move(request)));
  binding_->set_connection_error_handler([this]() { OnConnectionError(); });
}

void CommandBufferImpl::InitializeOnGpuThread(
    mojom::CommandBufferLostContextObserverPtr loss_observer,
    mojo::ScopedSharedBufferHandle shared_state,
    mojo::Array<int32_t> attribs,
    const base::Callback<void(mojom::CommandBufferInfoPtr)>& callback) {
  DCHECK(!driver_);
  driver_.reset(new CommandBufferDriver(
      gpu::CommandBufferNamespace::MOJO, ++g_next_command_buffer_id,
      gfx::kNullAcceleratedWidget, gpu_state_));
  driver_->set_client(make_scoped_ptr(new CommandBufferDriverClientImpl(this)));
  loss_observer_ = mojo::MakeProxy(loss_observer.PassInterface());
  bool result =
      driver_->Initialize(std::move(shared_state), std::move(attribs));
  mojom::CommandBufferInfoPtr info;
  if (result) {
    info = mojom::CommandBufferInfo::New();
    info->command_buffer_namespace = driver_->GetNamespaceID();
    info->command_buffer_id = driver_->GetCommandBufferID();
    info->capabilities =
        mojom::GpuCapabilities::From(driver_->GetCapabilities());
  }
  gpu_state_->control_task_runner()->PostTask(
      FROM_HERE, base::Bind(callback, base::Passed(&info)));
}

bool CommandBufferImpl::SetGetBufferOnGpuThread(int32_t buffer) {
  DCHECK(driver_->IsScheduled());
  driver_->SetGetBuffer(buffer);
  return true;
}

bool CommandBufferImpl::FlushOnGpuThread(int32_t put_offset,
                                         uint32_t order_num) {
  DCHECK(driver_->IsScheduled());
  driver_->sync_point_order_data()->BeginProcessingOrderNumber(order_num);
  driver_->Flush(put_offset);

  // Return false if the Flush is not finished, so the CommandBufferTaskRunner
  // will not remove this task from the task queue.
  const bool complete = !driver_->HasUnprocessedCommands();
  if (!complete)
    driver_->sync_point_order_data()->PauseProcessingOrderNumber(order_num);
  else
    driver_->sync_point_order_data()->FinishProcessingOrderNumber(order_num);
  return complete;
}

bool CommandBufferImpl::MakeProgressOnGpuThread(
    int32_t last_get_offset,
    const base::Callback<void(const gpu::CommandBuffer::State&)>& callback) {
  DCHECK(driver_->IsScheduled());
  gpu_state_->control_task_runner()->PostTask(
      FROM_HERE, base::Bind(callback, driver_->GetLastState()));
  return true;
}

bool CommandBufferImpl::RegisterTransferBufferOnGpuThread(
    int32_t id,
    mojo::ScopedSharedBufferHandle transfer_buffer,
    uint32_t size) {
  DCHECK(driver_->IsScheduled());
  driver_->RegisterTransferBuffer(id, std::move(transfer_buffer), size);
  return true;
}

bool CommandBufferImpl::DestroyTransferBufferOnGpuThread(int32_t id) {
  DCHECK(driver_->IsScheduled());
  driver_->DestroyTransferBuffer(id);
  return true;
}

bool CommandBufferImpl::RetireSyncPointOnGpuThread(uint32_t sync_point) {
  DCHECK(driver_->IsScheduled());
  gpu_state_->sync_point_manager()->RetireSyncPoint(sync_point);
  return true;
}

bool CommandBufferImpl::CreateImageOnGpuThread(int32_t id,
                                               mojo::ScopedHandle memory_handle,
                                               int32_t type,
                                               mojo::SizePtr size,
                                               int32_t format,
                                               int32_t internal_format) {
  DCHECK(driver_->IsScheduled());
  driver_->CreateImage(id, std::move(memory_handle), type, std::move(size),
                       format, internal_format);
  return true;
}

bool CommandBufferImpl::DestroyImageOnGpuThread(int32_t id) {
  DCHECK(driver_->IsScheduled());
  driver_->DestroyImage(id);
  return true;
}

void CommandBufferImpl::OnConnectionError() {
  // OnConnectionError() is called on the control thread |control_task_runner|.

  // Before deleting, we need to delete |binding_| because it is bound to the
  // current thread (|control_task_runner|).
  binding_.reset();

  // Objects we own (such as CommandBufferDriver) need to be destroyed on the
  // thread we were created on.
  gpu_state_->command_buffer_task_runner()->PostTask(
      driver_.get(), base::Bind(&CommandBufferImpl::DeleteOnGpuThread,
                                base::Unretained(this)));
}

bool CommandBufferImpl::DeleteOnGpuThread() {
  delete this;
  return true;
}

}  // namespace mus
