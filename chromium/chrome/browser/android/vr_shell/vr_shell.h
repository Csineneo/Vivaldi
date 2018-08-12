// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_

#include <jni.h>

#include <memory>

#include "base/android/jni_weak_ref.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/android/vr_shell/vr_math.h"
#include "device/vr/android/gvr/gvr_delegate.h"
#include "third_party/gvr-android-sdk/src/ndk/include/vr/gvr/capi/include/gvr.h"
#include "third_party/gvr-android-sdk/src/ndk/include/vr/gvr/capi/include/gvr_types.h"

namespace content {
class WebContents;
}

namespace ui {
class WindowAndroid;
}

namespace vr_shell {

class UiScene;
class VrCompositor;
class VrController;
class VrInputManager;
class VrShellDelegate;
class VrShellRenderer;
struct ContentRectangle;
struct VrGesture;

enum UiAction {
  HISTORY_BACK = 0,
  HISTORY_FORWARD,
  RELOAD,
  ZOOM_OUT,
  ZOOM_IN
};

class VrShell : public device::GvrDelegate {
 public:
  VrShell(JNIEnv* env, jobject obj,
          content::WebContents* main_contents,
          ui::WindowAndroid* content_window,
          content::WebContents* ui_contents,
          ui::WindowAndroid* ui_window);

  void UpdateCompositorLayers(JNIEnv* env,
                              const base::android::JavaParamRef<jobject>& obj);
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void SetDelegate(JNIEnv* env,
                   const base::android::JavaParamRef<jobject>& obj,
                   const base::android::JavaParamRef<jobject>& delegate);
  void GvrInit(JNIEnv* env,
               const base::android::JavaParamRef<jobject>& obj,
               jlong native_gvr_api);
  void InitializeGl(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    jint content_texture_handle,
                    jint ui_texture_handle);
  void DrawFrame(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnTriggerEvent(JNIEnv* env,
                      const base::android::JavaParamRef<jobject>& obj);
  void OnPause(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnResume(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void SetWebVrMode(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    bool enabled);

  // html/js UI hooks.
  static base::WeakPtr<VrShell> GetWeakPtr(
      const content::WebContents* web_contents);
  UiScene* GetScene();
  void OnDomContentsLoaded();

  // device::GvrDelegate implementation
  void SetWebVRSecureOrigin(bool secure_origin) override;
  void SubmitWebVRFrame() override;
  void UpdateWebVRTextureBounds(
      int eye, float left, float top, float width, float height) override;
  gvr::GvrApi* gvr_api() override;
  void SetGvrPoseForWebVr(const gvr::Mat4f& pose, uint32_t pose_num) override;

  void ContentSurfaceChanged(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& object,
      jint width,
      jint height,
      const base::android::JavaParamRef<jobject>& surface);
  void UiSurfaceChanged(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& object,
      jint width,
      jint height,
      const base::android::JavaParamRef<jobject>& surface);

  // Called from non-render thread to queue a callback onto the render thread.
  // The render thread checks for callbacks and processes them between frames.
  void QueueTask(base::Callback<void()>& callback);

  // Perform a UI action triggered by the javascript API.
  void DoUiAction(const UiAction action);

 private:
  virtual ~VrShell();
  void LoadUIContent();
  bool IsUiTextureReady();
  // Converts a pixel rectangle to (0..1) float texture coordinates.
  // Callers need to ensure that the texture width/height is
  // initialized by checking IsUiTextureReady() first.
  Rectf MakeUiGlCopyRect(Recti pixel_rect);
  void DrawVrShell(const gvr::Mat4f& head_pose);
  void DrawEye(const gvr::Mat4f& view_matrix,
               const gvr::BufferViewport& params);
  void DrawUI(const gvr::Mat4f& render_matrix);
  void DrawCursor(const gvr::Mat4f& render_matrix);
  void DrawWebVr();
  void DrawWebVrOverlay(int64_t present_time_nanos);
  void DrawWebVrEye(const gvr::Mat4f& view_matrix,
                    const gvr::BufferViewport& params,
                    int64_t present_time_nanos);

  void UpdateController(const gvr::Vec3f& forward_vector);

  void HandleQueuedTasks();

  // samplerExternalOES texture data for UI content image.
  jint ui_texture_id_ = 0;
  // samplerExternalOES texture data for main content image.
  jint content_texture_id_ = 0;

  float desktop_screen_tilt_;
  float desktop_height_;

  std::unique_ptr<UiScene> scene_;

  std::unique_ptr<gvr::GvrApi> gvr_api_;
  std::unique_ptr<gvr::BufferViewportList> buffer_viewport_list_;
  std::unique_ptr<gvr::BufferViewport> buffer_viewport_;
  std::unique_ptr<gvr::SwapChain> swap_chain_;

  gvr::Sizei render_size_;

  std::queue<base::Callback<void()>> task_queue_;
  base::Lock task_queue_lock_;

  std::unique_ptr<VrCompositor> content_compositor_;
  content::WebContents* main_contents_;
  std::unique_ptr<VrCompositor> ui_compositor_;
  content::WebContents* ui_contents_;

  VrShellDelegate* delegate_ = nullptr;
  std::unique_ptr<VrShellRenderer> vr_shell_renderer_;
  base::android::ScopedJavaGlobalRef<jobject> j_vr_shell_;

  bool touch_pending_ = false;
  gvr::Quatf controller_quat_;

  gvr::Vec3f target_point_;
  const ContentRectangle* target_element_ = nullptr;
  VrInputManager* current_input_target_ = nullptr;
  int ui_tex_width_ = 0;
  int ui_tex_height_ = 0;

  bool webvr_mode_ = false;
  bool webvr_secure_origin_ = false;
  int64_t webvr_warning_end_nanos_ = 0;
  // The pose ring buffer size must be a power of two to avoid glitches when
  // the pose index wraps around. It should be large enough to handle the
  // current backlog of poses which is 2-3 frames.
  static constexpr int kPoseRingBufferSize = 8;
  std::vector<gvr::Mat4f> webvr_head_pose_;

  std::unique_ptr<VrController> controller_;
  scoped_refptr<VrInputManager> content_input_manager_;
  scoped_refptr<VrInputManager> ui_input_manager_;

  base::WeakPtrFactory<VrShell> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(VrShell);
};

bool RegisterVrShell(JNIEnv* env);

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_
