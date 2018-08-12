// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_EXTENSION_HELPER_H_
#define EXTENSIONS_RENDERER_EXTENSION_HELPER_H_

#include <string>

#include "base/macros.h"
#include "content/public/renderer/render_view_observer.h"

#include "content/public/browser/web_contents.h"

namespace extensions {
class Dispatcher;

// RenderView-level plumbing for extension features.
class ExtensionHelper : public content::RenderViewObserver {
 public:
  ExtensionHelper(content::RenderView* render_view, Dispatcher* dispatcher);
  ~ExtensionHelper() override;

 private:
  // RenderViewObserver implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void DraggableRegionsChanged(blink::WebFrame* frame) override;
  void OnDestruct() override;

  void OnZoomVivaldiUI(double zoom_factor);
  void OnUpdateBrowserWindowId(int window_id);

  void OnAppWindowClosed();
  void OnSetFrameName(const std::string& name);

  typedef base::Callback<void(content::WebContents*)> CallbackWithWebcontents;

  // TODO : We need to pass a callback pluss the contents to be executed here.
  void OnWebContentsFromTabId(int );

  Dispatcher* dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionHelper);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_EXTENSION_HELPER_H_
