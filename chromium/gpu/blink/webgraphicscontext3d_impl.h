// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_BLINK_WEBGRAPHICSCONTEXT3D_IMPL_H_
#define GPU_BLINK_WEBGRAPHICSCONTEXT3D_IMPL_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "gpu/blink/gpu_blink_export.h"
#include "third_party/WebKit/public/platform/WebGraphicsContext3D.h"
#include "third_party/WebKit/public/platform/WebString.h"

namespace gpu {

namespace gles2 {
class GLES2Interface;
class GLES2ImplementationErrorMessageCallback;
struct ContextCreationAttribHelper;
}
}

namespace gpu_blink {

class WebGraphicsContext3DErrorMessageCallback;

class GPU_BLINK_EXPORT WebGraphicsContext3DImpl
    : public NON_EXPORTED_BASE(blink::WebGraphicsContext3D) {
 public:
  ~WebGraphicsContext3DImpl() override;

  //----------------------------------------------------------------------
  // WebGraphicsContext3D methods

  void setContextLostCallback(
      WebGraphicsContext3D::WebGraphicsContextLostCallback* callback);

  void setErrorMessageCallback(
      WebGraphicsContext3D::WebGraphicsErrorMessageCallback* callback) override;

  ::gpu::gles2::GLES2Interface* GetGLInterface() {
    return gl_;
  }

 protected:
  friend class WebGraphicsContext3DErrorMessageCallback;

  WebGraphicsContext3DImpl();

  ::gpu::gles2::GLES2ImplementationErrorMessageCallback*
      getErrorMessageCallback();
  virtual void OnErrorMessage(const std::string& message, int id);

  void SetGLInterface(::gpu::gles2::GLES2Interface* gl) { gl_ = gl; }

  bool initialized_;
  bool initialize_failed_;

  WebGraphicsContext3D::WebGraphicsContextLostCallback* context_lost_callback_;

  WebGraphicsContext3D::WebGraphicsErrorMessageCallback*
      error_message_callback_;
  scoped_ptr<WebGraphicsContext3DErrorMessageCallback>
      client_error_message_callback_;

  ::gpu::gles2::GLES2Interface* gl_;
  bool lose_context_when_out_of_memory_;
};

}  // namespace gpu_blink

#endif  // GPU_BLINK_WEBGRAPHICSCONTEXT3D_IMPL_H_
