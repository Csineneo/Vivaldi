// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/webgraphicscontext3d_provider_impl.h"

#include "cc/blink/context_provider_web_context.h"
#include "third_party/WebKit/public/platform/callback/WebClosure.h"

namespace content {

WebGraphicsContext3DProviderImpl::WebGraphicsContext3DProviderImpl(
    scoped_refptr<cc_blink::ContextProviderWebContext> provider)
    : provider_(provider) {
}

WebGraphicsContext3DProviderImpl::~WebGraphicsContext3DProviderImpl() {}

blink::WebGraphicsContext3D* WebGraphicsContext3DProviderImpl::context3d() {
  return provider_->WebContext3D();
}

gpu::gles2::GLES2Interface* WebGraphicsContext3DProviderImpl::contextGL() {
  return provider_->ContextGL();
}

GrContext* WebGraphicsContext3DProviderImpl::grContext() {
  return provider_->GrContext();
}

void WebGraphicsContext3DProviderImpl::setLostContextCallback(
    blink::WebClosure c) {
  provider_->SetLostContextCallback(c.TakeBaseClosure());
}

}  // namespace content
