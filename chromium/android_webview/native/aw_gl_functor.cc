// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_gl_functor.h"

#include "android_webview/browser/browser_view_renderer.h"
#include "android_webview/public/browser/draw_gl.h"
#include "content/public/browser/browser_thread.h"
#include "jni/AwGLFunctor_jni.h"

using base::android::AttachCurrentThread;
using content::BrowserThread;

extern "C" {
static AwDrawGLFunction DrawGLFunction;
static void DrawGLFunction(long view_context,
                           AwDrawGLInfo* draw_info,
                           void* spare) {
  // |view_context| is the value that was returned from the java
  // AwContents.onPrepareDrawGL; this cast must match the code there.
  reinterpret_cast<android_webview::RenderThreadManager*>(view_context)
      ->DrawGL(draw_info);
}
}

namespace android_webview {

AwGLFunctor::AwGLFunctor(const JavaObjectWeakGlobalRef& java_ref)
    : java_ref_(java_ref),
      render_thread_manager_(
          this,
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::UI)),
      browser_view_renderer_(nullptr) {}

AwGLFunctor::~AwGLFunctor() {}

void AwGLFunctor::OnParentDrawConstraintsUpdated() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (browser_view_renderer_)
    browser_view_renderer_->OnParentDrawConstraintsUpdated();
}

bool AwGLFunctor::RequestDrawGL(bool wait_for_completion) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return false;
  return Java_AwGLFunctor_requestDrawGL(env, obj.obj(), wait_for_completion);
}

void AwGLFunctor::DetachFunctorFromView() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (!obj.is_null())
    Java_AwGLFunctor_detachFunctorFromView(env, obj.obj());
}

void AwGLFunctor::SetBrowserViewRenderer(
    BrowserViewRenderer* browser_view_renderer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  browser_view_renderer_ = browser_view_renderer;
}

void AwGLFunctor::Destroy(JNIEnv* env,
                          const base::android::JavaParamRef<jobject>& obj) {
  java_ref_.reset();
  delete this;
}

void AwGLFunctor::DeleteHardwareRenderer(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  render_thread_manager_.DeleteHardwareRendererOnUI();
}

jlong AwGLFunctor::GetAwDrawGLViewContext(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  return reinterpret_cast<intptr_t>(&render_thread_manager_);
}

static jlong GetAwDrawGLFunction(JNIEnv* env, const JavaParamRef<jclass>&) {
  return reinterpret_cast<intptr_t>(&DrawGLFunction);
}

static jlong Create(JNIEnv* env,
                    const JavaParamRef<jclass>&,
                    const base::android::JavaParamRef<jobject>& obj) {
  return reinterpret_cast<intptr_t>(
      new AwGLFunctor(JavaObjectWeakGlobalRef(env, obj)));
}

bool RegisterAwGLFunctor(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android_webview
