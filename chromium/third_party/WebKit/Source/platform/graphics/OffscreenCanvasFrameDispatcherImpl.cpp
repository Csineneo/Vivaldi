// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/OffscreenCanvasFrameDispatcherImpl.h"

#include "cc/output/compositor_frame.h"
#include "cc/output/delegated_frame_data.h"
#include "cc/quads/render_pass.h"
#include "cc/quads/shared_quad_state.h"
#include "cc/quads/solid_color_draw_quad.h"
#include "cc/quads/texture_draw_quad.h"
#include "cc/resources/returned_resource.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/graphics/gpu/SharedGpuContext.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "public/platform/InterfaceProvider.h"
#include "public/platform/Platform.h"
#include "public/platform/WebGraphicsContext3DProvider.h"
#include "public/platform/modules/offscreencanvas/offscreen_canvas_surface.mojom-blink.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkXfermode.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/transform.h"
#include "wtf/typed_arrays/ArrayBuffer.h"
#include "wtf/typed_arrays/Uint8Array.h"

namespace blink {

OffscreenCanvasFrameDispatcherImpl::OffscreenCanvasFrameDispatcherImpl(
    uint32_t clientId,
    uint32_t sinkId,
    uint32_t localId,
    uint64_t nonce,
    int width,
    int height)
    : m_surfaceId(cc::FrameSinkId(clientId, sinkId),
                  cc::LocalFrameId(localId, nonce)),
      m_width(width),
      m_height(height),
      m_nextResourceId(1u),
      m_binding(this) {
  DCHECK(!m_sink.is_bound());
  mojom::blink::OffscreenCanvasCompositorFrameSinkProviderPtr provider;
  Platform::current()->interfaceProvider()->getInterface(
      mojo::GetProxy(&provider));
  provider->CreateCompositorFrameSink(m_surfaceId,
                                      m_binding.CreateInterfacePtrAndBind(),
                                      mojo::GetProxy(&m_sink));
}

// Case 1: both canvas and compositor are not gpu accelerated, or canvas is
// accelerated but --disable-gpu-compositing is specified, or
// WebGL's commit called with swiftshader. The last case is indicated by
// WebGraphicsContext3DProvider::isSoftwareRendering.
void OffscreenCanvasFrameDispatcherImpl::setTransferableResourceInMemory(
    cc::TransferableResource& resource,
    RefPtr<StaticBitmapImage> image) {
  std::unique_ptr<cc::SharedBitmap> bitmap =
      Platform::current()->allocateSharedBitmap(IntSize(m_width, m_height));
  if (!bitmap)
    return;
  unsigned char* pixels = bitmap->pixels();
  DCHECK(pixels);
  SkImageInfo imageInfo = SkImageInfo::Make(
      m_width, m_height, kN32_SkColorType,
      image->isPremultiplied() ? kPremul_SkAlphaType : kUnpremul_SkAlphaType);
  // TODO(xlai): Optimize to avoid copying pixels. See crbug.com/651456.
  // However, in the case when |image| is texture backed, this function call
  // does a GPU readback which is required.
  image->imageForCurrentFrame()->readPixels(imageInfo, pixels,
                                            imageInfo.minRowBytes(), 0, 0);
  resource.mailbox_holder.mailbox = bitmap->id();
  resource.mailbox_holder.texture_target = 0;
  resource.is_software = true;

  // Hold ref to |bitmap|, to keep it alive until the browser ReturnResources.
  // It guarantees that the shared bitmap is not re-used or deleted.
  m_sharedBitmaps.add(m_nextResourceId, std::move(bitmap));
}

// Case 2: canvas is not gpu-accelerated, but compositor is
void OffscreenCanvasFrameDispatcherImpl::setTransferableResourceMemoryToTexture(
    cc::TransferableResource& resource,
    RefPtr<StaticBitmapImage> image) {
  // TODO(crbug.com/652707): When committing the first frame, there is no
  // instance of SharedGpuContext yet, calling SharedGpuContext::gl() will
  // trigger a creation of an instace, which requires to create a
  // WebGraphicsContext3DProvider. This process is quite expensive, because
  // WebGraphicsContext3DProvider can only be constructed on the main thread,
  // and bind to the worker thread if commit() is called on worker. In the
  // subsequent frame, we should already have a SharedGpuContext, then getting
  // the gl interface should not be expensive.
  gpu::gles2::GLES2Interface* gl = SharedGpuContext::gl();

  SkImageInfo info = SkImageInfo::Make(
      m_width, m_height, kN32_SkColorType,
      image->isPremultiplied() ? kPremul_SkAlphaType : kUnpremul_SkAlphaType);
  RefPtr<ArrayBuffer> dstBuffer =
      ArrayBuffer::createOrNull(m_width * m_height, info.bytesPerPixel());
  // If it fails to create a buffer for copying the pixel data, then exit early.
  if (!dstBuffer)
    return;
  RefPtr<Uint8Array> dstPixels =
      Uint8Array::create(dstBuffer, 0, dstBuffer->byteLength());
  image->imageForCurrentFrame()->readPixels(info, dstPixels->data(),
                                            info.minRowBytes(), 0, 0);

  GLuint textureId = 0u;
  gl->GenTextures(1, &textureId);
  gl->BindTexture(GL_TEXTURE_2D, textureId);
  GLenum format =
      (kN32_SkColorType == kRGBA_8888_SkColorType) ? GL_RGBA : GL_BGRA_EXT;
  gl->TexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0, format,
                 GL_UNSIGNED_BYTE, 0);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  // The pixel data will be uploaded to GPU memory, we have to keep the GPU
  // memory alive until browser ReturnResources, so here we put textureId for
  // that piece of GPU memory into a hashmap.
  m_cachedTextureIds.add(m_nextResourceId, textureId);
  gl->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, format,
                    GL_UNSIGNED_BYTE, dstPixels->data());

  gpu::Mailbox mailbox;
  gl->GenMailboxCHROMIUM(mailbox.name);
  gl->ProduceTextureCHROMIUM(GL_TEXTURE_2D, mailbox.name);

  const GLuint64 fenceSync = gl->InsertFenceSyncCHROMIUM();
  gl->ShallowFlushCHROMIUM();
  gpu::SyncToken syncToken;
  gl->GenSyncTokenCHROMIUM(fenceSync, syncToken.GetData());

  resource.mailbox_holder =
      gpu::MailboxHolder(mailbox, syncToken, GL_TEXTURE_2D);
  resource.read_lock_fences_enabled = false;
  resource.is_software = false;
}

// Case 3: both canvas and compositor are gpu accelerated.
void OffscreenCanvasFrameDispatcherImpl::setTransferableResourceInTexture(
    cc::TransferableResource& resource,
    RefPtr<StaticBitmapImage> image) {
  image->ensureMailbox();
  resource.mailbox_holder = gpu::MailboxHolder(
      image->getMailbox(), image->getSyncToken(), GL_TEXTURE_2D);
  resource.read_lock_fences_enabled = false;
  resource.is_software = false;

  // Hold ref to |image|, to keep it alive until the browser ReturnResources.
  // It guarantees that the resource is not re-used or deleted.
  m_cachedImages.add(m_nextResourceId, std::move(image));
}

// When WebGL's commit is called on SwiftShader, we have software rendered
// WebGL.
void OffscreenCanvasFrameDispatcherImpl::dispatchFrame(
    RefPtr<StaticBitmapImage> image,
    bool isWebGLSoftwareRendering) {
  if (!image)
    return;
  if (!verifyImageSize(image->imageForCurrentFrame()))
    return;
  cc::CompositorFrame frame;
  // TODO(crbug.com/652931): update the device_scale_factor
  frame.metadata.device_scale_factor = 1.0f;
  frame.delegated_frame_data.reset(new cc::DelegatedFrameData);

  const gfx::Rect bounds(m_width, m_height);
  const cc::RenderPassId renderPassId(1, 1);
  std::unique_ptr<cc::RenderPass> pass = cc::RenderPass::Create();
  pass->SetAll(renderPassId, bounds, bounds, gfx::Transform(), false);

  cc::SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
  sqs->SetAll(gfx::Transform(), bounds.size(), bounds, bounds, false, 1.f,
              SkXfermode::kSrcOver_Mode, 0);

  cc::TransferableResource resource;
  resource.id = m_nextResourceId;
  resource.format = cc::ResourceFormat::RGBA_8888;
  // TODO(crbug.com/645590): filter should respect the image-rendering CSS
  // property of associated canvas element.
  resource.filter = GL_LINEAR;
  resource.size = gfx::Size(m_width, m_height);
  // TODO(crbug.com/646022): making this overlay-able.
  resource.is_overlay_candidate = false;

  if (!image->isTextureBacked() &&
      !Platform::current()->isGPUCompositingEnabled())
    setTransferableResourceInMemory(resource, image);
  else if (!image->isTextureBacked() &&
           Platform::current()->isGPUCompositingEnabled())
    setTransferableResourceMemoryToTexture(resource, image);
  else if (image->isTextureBacked() &&
           (!Platform::current()->isGPUCompositingEnabled() ||
            isWebGLSoftwareRendering))
    setTransferableResourceInMemory(resource, image);
  else
    setTransferableResourceInTexture(resource, image);

  m_nextResourceId++;
  frame.delegated_frame_data->resource_list.push_back(std::move(resource));

  cc::TextureDrawQuad* quad =
      pass->CreateAndAppendDrawQuad<cc::TextureDrawQuad>();
  gfx::Size rectSize(m_width, m_height);

  const bool needsBlending = true;
  // TOOD(crbug.com/645993): this should be inherited from WebGL context's
  // creation settings.
  const bool premultipliedAlpha = true;
  const gfx::PointF uvTopLeft(0.f, 0.f);
  const gfx::PointF uvBottomRight(1.f, 1.f);
  float vertexOpacity[4] = {1.f, 1.f, 1.f, 1.f};
  const bool yflipped = false;
  // TODO(crbug.com/645994): this should be true when using style
  // "image-rendering: pixelated".
  const bool nearestNeighbor = false;
  quad->SetAll(sqs, bounds, bounds, bounds, needsBlending, resource.id,
               gfx::Size(), premultipliedAlpha, uvTopLeft, uvBottomRight,
               SK_ColorTRANSPARENT, vertexOpacity, yflipped, nearestNeighbor,
               false);

  frame.delegated_frame_data->render_pass_list.push_back(std::move(pass));

  m_sink->SubmitCompositorFrame(std::move(frame), base::Closure());
}

void OffscreenCanvasFrameDispatcherImpl::ReturnResources(
    Vector<cc::mojom::blink::ReturnedResourcePtr> resources) {
  for (const auto& resource : resources) {
    m_cachedImages.remove(resource->id);
    m_sharedBitmaps.remove(resource->id);
    m_cachedTextureIds.remove(resource->id);
  }
}

bool OffscreenCanvasFrameDispatcherImpl::verifyImageSize(
    const sk_sp<SkImage>& image) {
  if (image && image->width() == m_width && image->height() == m_height)
    return true;
  return false;
}

}  // namespace blink
