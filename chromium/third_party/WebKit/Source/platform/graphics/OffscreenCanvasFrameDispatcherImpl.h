// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OffscreenCanvasFrameDispatcherImpl_h
#define OffscreenCanvasFrameDispatcherImpl_h

#include "cc/ipc/mojo_compositor_frame_sink.mojom-blink.h"
#include "cc/resources/shared_bitmap.h"
#include "cc/surfaces/surface_id.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "platform/graphics/OffscreenCanvasFrameDispatcher.h"
#include "platform/graphics/StaticBitmapImage.h"
#include "wtf/Compiler.h"
#include <memory>

namespace blink {

class PLATFORM_EXPORT OffscreenCanvasFrameDispatcherImpl final
    : public OffscreenCanvasFrameDispatcher,
      WTF_NON_EXPORTED_BASE(
          public cc::mojom::blink::MojoCompositorFrameSinkClient) {
 public:
  OffscreenCanvasFrameDispatcherImpl(uint32_t clientId,
                                     uint32_t sinkId,
                                     uint32_t localId,
                                     uint64_t nonce,
                                     int width,
                                     int height);

  // OffscreenCanvasFrameDispatcher implementation.
  ~OffscreenCanvasFrameDispatcherImpl() override {}
  void dispatchFrame(RefPtr<StaticBitmapImage>,
                     bool isWebGLSoftwareRendering = false) override;

  // cc::mojom::blink::MojoCompositorFrameSinkClient implementation.
  void ReturnResources(
      Vector<cc::mojom::blink::ReturnedResourcePtr> resources) override;

 private:
  const cc::SurfaceId m_surfaceId;
  const int m_width;
  const int m_height;

  unsigned m_nextResourceId;
  HashMap<unsigned, RefPtr<StaticBitmapImage>> m_cachedImages;
  HashMap<unsigned, std::unique_ptr<cc::SharedBitmap>> m_sharedBitmaps;
  HashMap<unsigned, GLuint> m_cachedTextureIds;

  bool verifyImageSize(const sk_sp<SkImage>&);

  cc::mojom::blink::MojoCompositorFrameSinkPtr m_sink;
  mojo::Binding<cc::mojom::blink::MojoCompositorFrameSinkClient> m_binding;

  void setTransferableResourceInMemory(cc::TransferableResource&,
                                       RefPtr<StaticBitmapImage>);
  void setTransferableResourceMemoryToTexture(cc::TransferableResource&,
                                              RefPtr<StaticBitmapImage>);
  void setTransferableResourceInTexture(cc::TransferableResource&,
                                        RefPtr<StaticBitmapImage>);
};

}  // namespace blink

#endif  // OffscreenCanvasFrameDispatcherImpl_h
