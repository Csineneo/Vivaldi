// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/media/avda_codec_image.h"

#include <string.h>

#include "content/common/gpu/media/avda_shared_state.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/context_state.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "ui/gl/android/surface_texture.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/scoped_make_current.h"

namespace content {

AVDACodecImage::AVDACodecImage(
    int picture_buffer_id,
    const scoped_refptr<AVDASharedState>& shared_state,
    media::VideoCodecBridge* codec,
    const base::WeakPtr<gpu::gles2::GLES2Decoder>& decoder,
    const scoped_refptr<gfx::SurfaceTexture>& surface_texture)
    : shared_state_(shared_state),
      codec_buffer_index_(kInvalidCodecBufferIndex),
      media_codec_(codec),
      decoder_(decoder),
      surface_texture_(surface_texture),
      detach_surface_texture_on_destruction_(false),
      texture_(0),
      picture_buffer_id_(picture_buffer_id) {
  // Default to a sane guess of "flip Y", just in case we can't get
  // the matrix on the first call.
  memset(gl_matrix_, 0, sizeof(gl_matrix_));
  gl_matrix_[0] = gl_matrix_[10] = gl_matrix_[15] = 1.0f;
  gl_matrix_[5] = -1.0f;
  shared_state_->SetImageForPicture(picture_buffer_id_, this);
}

AVDACodecImage::~AVDACodecImage() {
  shared_state_->SetImageForPicture(picture_buffer_id_, nullptr);
}

void AVDACodecImage::Destroy(bool have_context) {}

gfx::Size AVDACodecImage::GetSize() {
  return size_;
}

unsigned AVDACodecImage::GetInternalFormat() {
  return GL_RGBA;
}

bool AVDACodecImage::BindTexImage(unsigned target) {
  return false;
}

void AVDACodecImage::ReleaseTexImage(unsigned target) {}

bool AVDACodecImage::CopyTexImage(unsigned target) {
  if (!surface_texture_)
    return false;

  if (target != GL_TEXTURE_EXTERNAL_OES)
    return false;

  GLint bound_service_id = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &bound_service_id);
  // We insist that the currently bound texture is the right one.  We could
  // make a new glimage from a 2D image.
  if (bound_service_id != shared_state_->surface_texture_service_id())
    return false;

  // If the surface texture isn't attached yet, then attach it.  Note that this
  // will be to the texture in |shared_state_|, because of the checks above.
  if (!shared_state_->surface_texture_is_attached())
    AttachSurfaceTextureToContext();

  // Make sure that we have the right image in the front buffer.  Note that the
  // bound_service_id is guaranteed to be equal to the surface texture's client
  // texture id, so we can skip preserving it if the right context is current.
  UpdateSurfaceInternal(UpdateMode::RENDER_TO_FRONT_BUFFER,
                        kDontRestoreBindings);

  // By setting image state to UNBOUND instead of COPIED we ensure that
  // CopyTexImage() is called each time the surface texture is used for drawing.
  // It would be nice if we could do this via asking for the currently bound
  // Texture, but the active unit never seems to change.
  texture_->SetLevelStreamTextureImage(GL_TEXTURE_EXTERNAL_OES, 0, this,
                                       gpu::gles2::Texture::UNBOUND);

  return true;
}

bool AVDACodecImage::CopyTexSubImage(unsigned target,
                                     const gfx::Point& offset,
                                     const gfx::Rect& rect) {
  return false;
}

bool AVDACodecImage::ScheduleOverlayPlane(gfx::AcceleratedWidget widget,
                                          int z_order,
                                          gfx::OverlayTransform transform,
                                          const gfx::Rect& bounds_rect,
                                          const gfx::RectF& crop_rect) {
  // This should only be called when we're rendering to a SurfaceView.
  if (surface_texture_) {
    DVLOG(1) << "Invalid call to ScheduleOverlayPlane; this image is "
                "SurfaceTexture backed.";
    return false;
  }

  UpdateSurface(UpdateMode::RENDER_TO_FRONT_BUFFER);
  return true;
}

void AVDACodecImage::OnMemoryDump(base::trace_event::ProcessMemoryDump* pmd,
                                  uint64_t process_tracing_id,
                                  const std::string& dump_name) {}

void AVDACodecImage::UpdateSurfaceTexture(RestoreBindingsMode mode) {
  DCHECK(surface_texture_);
  DCHECK_EQ(codec_buffer_index_, kUpdateOnly);
  codec_buffer_index_ = kRendered;

  // Swap the rendered image to the front.
  scoped_ptr<ui::ScopedMakeCurrent> scoped_make_current = MakeCurrentIfNeeded();

  // If we changed contexts, then we always want to restore it, since the caller
  // doesn't know that we're switching contexts.
  if (scoped_make_current)
    mode = kDoRestoreBindings;

  // Save the current binding if requested.
  GLint bound_service_id = 0;
  if (mode == kDoRestoreBindings)
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &bound_service_id);

  surface_texture_->UpdateTexImage();
  if (mode == kDoRestoreBindings)
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, bound_service_id);

  // Helpfully, this is already column major.
  surface_texture_->GetTransformMatrix(gl_matrix_);
}

void AVDACodecImage::UpdateSurface(UpdateMode update_mode) {
  UpdateSurfaceInternal(update_mode, kDoRestoreBindings);
}

void AVDACodecImage::CodecChanged(media::MediaCodecBridge* codec) {
  media_codec_ = codec;
  codec_buffer_index_ = kInvalidCodecBufferIndex;
}

void AVDACodecImage::UpdateSurfaceInternal(
    UpdateMode update_mode,
    RestoreBindingsMode attached_bindings_mode) {
  if (!IsCodecBufferOutstanding())
    return;

  ReleaseOutputBuffer(update_mode);

  // SurfaceViews are updated implicitly, so no further steps are necessary.
  if (!surface_texture_) {
    DCHECK(update_mode != UpdateMode::RENDER_TO_BACK_BUFFER);
    return;
  }

  // If front buffer rendering hasn't been requested, exit early.
  if (update_mode != UpdateMode::RENDER_TO_FRONT_BUFFER)
    return;

  // Surface texture is already attached, so just update it.
  if (shared_state_->surface_texture_is_attached()) {
    UpdateSurfaceTexture(attached_bindings_mode);
    return;
  }

  // Don't attach the surface texture permanently.  Perhaps we should just
  // attach the surface texture in avda and be done with it.
  GLuint service_id = 0;
  glGenTextures(1, &service_id);
  GLint bound_service_id = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &bound_service_id);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, service_id);
  AttachSurfaceTextureToContext();
  UpdateSurfaceTexture(kDontRestoreBindings);

  // Detach the surface texture, which deletes the generated texture.
  surface_texture_->DetachFromGLContext();
  shared_state_->DidDetachSurfaceTexture();
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, bound_service_id);
}

void AVDACodecImage::ReleaseOutputBuffer(UpdateMode update_mode) {
  DCHECK(IsCodecBufferOutstanding());

  // In case of discard, simply discard and clear our codec buffer index.
  if (update_mode == UpdateMode::DISCARD_CODEC_BUFFER) {
    if (codec_buffer_index_ != kUpdateOnly)
      media_codec_->ReleaseOutputBuffer(codec_buffer_index_, false);

    // Note: No need to wait for the frame to be available in the kUpdateOnly
    // case since it will be or has been waited on by another release call.
    codec_buffer_index_ = kInvalidCodecBufferIndex;
    return;
  }

  DCHECK(update_mode == UpdateMode::RENDER_TO_BACK_BUFFER ||
         update_mode == UpdateMode::RENDER_TO_FRONT_BUFFER);

  if (!surface_texture_) {
    DCHECK(update_mode == UpdateMode::RENDER_TO_FRONT_BUFFER);
    DCHECK_GE(codec_buffer_index_, 0);
    media_codec_->ReleaseOutputBuffer(codec_buffer_index_, true);
    codec_buffer_index_ = kRendered;
    return;
  }

  // If we've already released to the back buffer, there's nothing left to do,
  // but wait for the previously released buffer if necessary.
  if (codec_buffer_index_ != kUpdateOnly) {
    DCHECK(surface_texture_);
    DCHECK_GE(codec_buffer_index_, 0);
    shared_state_->RenderCodecBufferToSurfaceTexture(media_codec_,
                                                     codec_buffer_index_);
    codec_buffer_index_ = kUpdateOnly;
  }

  // Only wait for the SurfaceTexture update if we're rendering to the front.
  if (update_mode == UpdateMode::RENDER_TO_FRONT_BUFFER)
    shared_state_->WaitForFrameAvailable();
}

void AVDACodecImage::AttachSurfaceTextureToContext() {
  DCHECK(surface_texture_);

  // We assume that the currently bound texture is the intended one.

  // Attach the surface texture to the first context we're bound on, so that
  // no context switch is needed later.
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // The surface texture is already detached, so just attach it.
  // We could do this earlier, but SurfaceTexture has context affinity, and we
  // don't want to require a context switch.
  surface_texture_->AttachToGLContext();
  shared_state_->DidAttachSurfaceTexture();
}

scoped_ptr<ui::ScopedMakeCurrent> AVDACodecImage::MakeCurrentIfNeeded() {
  DCHECK(shared_state_->context());
  scoped_ptr<ui::ScopedMakeCurrent> scoped_make_current;
  if (!shared_state_->context()->IsCurrent(NULL)) {
    scoped_make_current.reset(new ui::ScopedMakeCurrent(
        shared_state_->context(), shared_state_->surface()));
  }

  return scoped_make_current;
}

void AVDACodecImage::GetTextureMatrix(float matrix[16]) {
  // Our current matrix may be stale.  Update it if possible.
  if (surface_texture_)
    UpdateSurface(UpdateMode::RENDER_TO_FRONT_BUFFER);
  memcpy(matrix, gl_matrix_, sizeof(gl_matrix_));
}

bool AVDACodecImage::IsCodecBufferOutstanding() const {
  static_assert(kUpdateOnly < 0 && kUpdateOnly > kRendered &&
                    kRendered > kInvalidCodecBufferIndex,
                "Codec buffer index enum values are not ordered correctly.");
  return codec_buffer_index_ > kRendered && media_codec_;
}

}  // namespace content
