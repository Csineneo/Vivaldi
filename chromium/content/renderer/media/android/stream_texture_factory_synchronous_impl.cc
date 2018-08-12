// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/android/stream_texture_factory_synchronous_impl.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "cc/output/context_provider.h"
#include "content/renderer/render_thread_impl.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/ipc/common/android/surface_texture_peer.h"
#include "ui/gl/android/surface_texture.h"

using gpu::gles2::GLES2Interface;

namespace content {

namespace {

class StreamTextureProxyImpl
    : public StreamTextureProxy,
      public base::SupportsWeakPtr<StreamTextureProxyImpl> {
 public:
  explicit StreamTextureProxyImpl(
      StreamTextureFactorySynchronousImpl::ContextProvider* provider);
  ~StreamTextureProxyImpl() override;

  // StreamTextureProxy implementation:
  void BindToLoop(int32_t stream_id,
                  cc::VideoFrameProvider::Client* client,
                  scoped_refptr<base::SingleThreadTaskRunner> loop) override;
  void Release() override;

 private:
  void BindOnThread(int32_t stream_id);
  void OnFrameAvailable();

  // Protects access to |client_| and |loop_|.
  base::Lock lock_;
  cc::VideoFrameProvider::Client* client_;
  scoped_refptr<base::SingleThreadTaskRunner> loop_;

  // Accessed on the |loop_| thread only.
  base::Closure callback_;
  scoped_refptr<StreamTextureFactorySynchronousImpl::ContextProvider>
      context_provider_;
  scoped_refptr<gfx::SurfaceTexture> surface_texture_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(StreamTextureProxyImpl);
};

StreamTextureProxyImpl::StreamTextureProxyImpl(
    StreamTextureFactorySynchronousImpl::ContextProvider* provider)
    : client_(NULL), context_provider_(provider) {
}

StreamTextureProxyImpl::~StreamTextureProxyImpl() {}

void StreamTextureProxyImpl::Release() {
  {
    // Cannot call into |client_| anymore (from any thread) after returning
    // from here.
    base::AutoLock lock(lock_);
    client_ = NULL;
  }
  // Release is analogous to the destructor, so there should be no more external
  // calls to this object in Release. Therefore there is no need to acquire the
  // lock to access |loop_|.
  if (!loop_.get() || loop_->BelongsToCurrentThread() ||
      !loop_->DeleteSoon(FROM_HERE, this)) {
    delete this;
  }
}

void StreamTextureProxyImpl::BindToLoop(
    int32_t stream_id,
    cc::VideoFrameProvider::Client* client,
    scoped_refptr<base::SingleThreadTaskRunner> loop) {
  DCHECK(loop.get());

  {
    base::AutoLock lock(lock_);
    DCHECK(!loop_.get() || (loop.get() == loop_.get()));
    loop_ = loop;
    client_ = client;
  }

  if (loop->BelongsToCurrentThread()) {
    BindOnThread(stream_id);
    return;
  }
  // Unretained is safe here only because the object is deleted on |loop_|
  // thread.
  loop->PostTask(FROM_HERE,
                 base::Bind(&StreamTextureProxyImpl::BindOnThread,
                            base::Unretained(this),
                            stream_id));
}

void StreamTextureProxyImpl::BindOnThread(int32_t stream_id) {
  surface_texture_ = context_provider_->GetSurfaceTexture(stream_id);
  if (!surface_texture_.get()) {
    LOG(ERROR) << "Failed to get SurfaceTexture for stream.";
    return;
  }

  callback_ =
      base::Bind(&StreamTextureProxyImpl::OnFrameAvailable, AsWeakPtr());
  surface_texture_->SetFrameAvailableCallback(callback_);
}

void StreamTextureProxyImpl::OnFrameAvailable() {
  base::AutoLock lock(lock_);
  if (client_)
    client_->DidReceiveFrame();
}

}  // namespace

// static
scoped_refptr<StreamTextureFactorySynchronousImpl>
StreamTextureFactorySynchronousImpl::Create(
    const CreateContextProviderCallback& try_create_callback) {
  return new StreamTextureFactorySynchronousImpl(try_create_callback);
}

StreamTextureFactorySynchronousImpl::StreamTextureFactorySynchronousImpl(
    const CreateContextProviderCallback& try_create_callback)
    : create_context_provider_callback_(try_create_callback) {}

StreamTextureFactorySynchronousImpl::~StreamTextureFactorySynchronousImpl() {}

StreamTextureProxy* StreamTextureFactorySynchronousImpl::CreateProxy() {
  bool had_proxy = !!context_provider_.get();
  if (!had_proxy)
    context_provider_ = create_context_provider_callback_.Run();

  if (!context_provider_.get())
    return NULL;

  if (!observers_.empty() && !had_proxy) {
    for (auto& observer : observers_)
      context_provider_->AddObserver(observer);
  }
  return new StreamTextureProxyImpl(context_provider_.get());
}

void StreamTextureFactorySynchronousImpl::EstablishPeer(int32_t stream_id,
                                                        int player_id,
                                                        int frame_id) {
  DCHECK(context_provider_.get());
  scoped_refptr<gfx::SurfaceTexture> surface_texture =
      context_provider_->GetSurfaceTexture(stream_id);
  if (surface_texture.get()) {
    gpu::SurfaceTexturePeer::GetInstance()->EstablishSurfaceTexturePeer(
        base::GetCurrentProcessHandle(), surface_texture, frame_id, player_id);
  }
}

unsigned StreamTextureFactorySynchronousImpl::CreateStreamTexture(
    unsigned texture_target,
    unsigned* texture_id,
    gpu::Mailbox* texture_mailbox) {
  DCHECK(context_provider_.get());
  unsigned stream_id = 0;
  GLES2Interface* gl = context_provider_->ContextGL();
  gl->GenTextures(1, texture_id);
  gl->ShallowFlushCHROMIUM();
  stream_id = context_provider_->CreateStreamTexture(*texture_id);
  gl->GenMailboxCHROMIUM(texture_mailbox->name);
  gl->ProduceTextureDirectCHROMIUM(
      *texture_id, texture_target, texture_mailbox->name);
  return stream_id;
}

void StreamTextureFactorySynchronousImpl::SetStreamTextureSize(
    int32_t stream_id,
    const gfx::Size& size) {}

gpu::gles2::GLES2Interface* StreamTextureFactorySynchronousImpl::ContextGL() {
  DCHECK(context_provider_.get());
  return context_provider_->ContextGL();
}

void StreamTextureFactorySynchronousImpl::AddObserver(
    StreamTextureFactoryContextObserver* obs) {
  DCHECK(observers_.find(obs) == observers_.end());
  observers_.insert(obs);
  if (context_provider_.get())
    context_provider_->AddObserver(obs);
}

void StreamTextureFactorySynchronousImpl::RemoveObserver(
    StreamTextureFactoryContextObserver* obs) {
  DCHECK(observers_.find(obs) != observers_.end());
  observers_.erase(obs);
  if (context_provider_.get())
    context_provider_->RemoveObserver(obs);
}

}  // namespace content
