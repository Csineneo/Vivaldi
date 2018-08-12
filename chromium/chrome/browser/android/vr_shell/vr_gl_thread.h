// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_GL_THREAD_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_GL_THREAD_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr_types.h"

namespace vr_shell {

class UiScene;
class UiSceneManager;
class VrShell;
class VrShellGl;

class VrGLThread : public base::Thread {
 public:
  VrGLThread(
      const base::WeakPtr<VrShell>& weak_vr_shell,
      scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner,
      gvr_context* gvr_api,
      bool initially_web_vr,
      bool reprojected_rendering);

  ~VrGLThread() override;
  base::WeakPtr<VrShellGl> GetVrShellGl() { return weak_vr_shell_gl_; }
  base::WeakPtr<UiSceneManager> GetSceneManager() {
    return weak_scene_manager_;
  }

 protected:
  void Init() override;
  void CleanUp() override;

 private:
  // Created on GL thread.
  std::unique_ptr<UiScene> scene_;
  std::unique_ptr<UiSceneManager> scene_manager_;
  base::WeakPtr<UiSceneManager> weak_scene_manager_;
  std::unique_ptr<VrShellGl> vr_shell_gl_;
  base::WeakPtr<VrShellGl> weak_vr_shell_gl_;

  // This state is used for initializing vr_shell_gl_.
  base::WeakPtr<VrShell> weak_vr_shell_;
  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;
  gvr_context* gvr_api_;
  bool initially_web_vr_;
  bool reprojected_rendering_;

  DISALLOW_COPY_AND_ASSIGN(VrGLThread);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_GL_THREAD_H_
