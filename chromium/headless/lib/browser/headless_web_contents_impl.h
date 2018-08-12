// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEADLESS_LIB_BROWSER_HEADLESS_WEB_CONTENTS_IMPL_H_
#define HEADLESS_LIB_BROWSER_HEADLESS_WEB_CONTENTS_IMPL_H_

#include "headless/public/headless_web_contents.h"

#include <memory>
#include <unordered_map>

namespace aura {
class Window;
}

namespace content {
class WebContents;
class BrowserContext;
}

namespace gfx {
class Size;
}

namespace headless {
class WebContentsObserverAdapter;

class HeadlessWebContentsImpl : public HeadlessWebContents {
 public:
  HeadlessWebContentsImpl(content::BrowserContext* context,
                          aura::Window* parent_window,
                          const gfx::Size& initial_size);
  ~HeadlessWebContentsImpl() override;

  // HeadlessWebContents implementation:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  content::WebContents* web_contents() const;
  bool OpenURL(const GURL& url);

 private:
  class Delegate;
  std::unique_ptr<Delegate> web_contents_delegate_;
  std::unique_ptr<content::WebContents> web_contents_;

  using ObserverMap =
      std::unordered_map<HeadlessWebContents::Observer*,
                         std::unique_ptr<WebContentsObserverAdapter>>;
  ObserverMap observer_map_;

  DISALLOW_COPY_AND_ASSIGN(HeadlessWebContentsImpl);
};

}  // namespace headless

#endif  // HEADLESS_LIB_BROWSER_HEADLESS_WEB_CONTENTS_IMPL_H_
