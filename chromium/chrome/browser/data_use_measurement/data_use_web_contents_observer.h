// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DATA_USE_MEASUREMENT_DATA_USE_WEB_CONTENTS_OBSERVER_H_
#define CHROME_BROWSER_DATA_USE_MEASUREMENT_DATA_USE_WEB_CONTENTS_OBSERVER_H_

#include "base/macros.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class RenderFrameHost;
}

namespace data_use_measurement {

class ChromeDataUseAscriberService;

// Propagates navigation and frame events to ChromeDataUseAscriberService.
// We use this class instead of having ChromeDataUseAscriberService derive from
// WebContentsObserver because each instance of WebContents needs its own
// instance of WebContentsObserver, and ChromeDataUseAscriberService needs to
// observe all WebContents.
class DataUseWebContentsObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<DataUseWebContentsObserver> {
 public:
  // Creates a DataUseWebContentsObserver for the given WebContents.
  static void CreateForWebContents(content::WebContents* web_contents);

  ~DataUseWebContentsObserver() override;

  // WebContentsObserver:
  // Called when a render frame host is created.
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;

  // Called when a render frame host is deleted.
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;

  // Called when a navigation is started.
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;

  // Called when a navigation is completed.
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // Called when a navigation is redirected.
  void DidRedirectNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<DataUseWebContentsObserver>;

  DataUseWebContentsObserver(content::WebContents* web_contents,
                             ChromeDataUseAscriberService* service);
  ChromeDataUseAscriberService* const service_;

  DISALLOW_COPY_AND_ASSIGN(DataUseWebContentsObserver);
};

}  // namespace data_use_measurement

#endif  // CHROME_BROWSER_DATA_USE_MEASUREMENT_DATA_USE_WEB_CONTENTS_OBSERVER_H_
