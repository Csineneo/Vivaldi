// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_CONTENT_BROWSER_TEST_UTILS_INTERNAL_H_
#define CONTENT_TEST_CONTENT_BROWSER_TEST_UTILS_INTERNAL_H_

// A collection of functions designed for use with content_shell based browser
// tests internal to the content/ module.
// Note: If a function here also works with browser_tests, it should be in
// the content public API.

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "cc/surfaces/surface_id.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/resource_dispatcher_host_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"

namespace cc {
class SurfaceManager;
}

namespace content {

class FrameTreeNode;
class MessageLoopRunner;
class RenderWidgetHostViewChildFrame;
class Shell;
class SiteInstance;
class ToRenderFrameHost;

// Navigates the frame represented by |node| to |url|, blocking until the
// navigation finishes.
void NavigateFrameToURL(FrameTreeNode* node, const GURL& url);

// Sets the DialogManager to proceed by default or not when showing a
// BeforeUnload dialog.
void SetShouldProceedOnBeforeUnload(Shell* shell, bool proceed);

// Creates compact textual representations of the state of the frame tree that
// is appropriate for use in assertions.
//
// The diagrams show frame tree structure, the SiteInstance of current frames,
// presence of pending frames, and the SiteInstances of any and all proxies.
// They look like this:
//
//        Site A (D pending) -- proxies for B C
//          |--Site B --------- proxies for A C
//          +--Site C --------- proxies for B A
//               |--Site A ---- proxies for B
//               +--Site A ---- proxies for B
//                    +--Site A -- proxies for B
//       Where A = http://127.0.0.1/
//             B = http://foo.com/ (no process)
//             C = http://bar.com/
//             D = http://next.com/
//
// SiteInstances are assigned single-letter names (A, B, C) which are remembered
// across invocations of the pretty-printer.
class FrameTreeVisualizer {
 public:
  FrameTreeVisualizer();
  ~FrameTreeVisualizer();

  // Formats and returns a diagram for the provided FrameTreeNode.
  std::string DepictFrameTree(FrameTreeNode* root);

 private:
  // Assign or retrive the abbreviated short name (A, B, C) for a site instance.
  std::string GetName(SiteInstance* site_instance);

  // Elements are site instance ids. The index of the SiteInstance in the vector
  // determines the abbreviated name (0->A, 1->B) for that SiteInstance.
  std::vector<int> seen_site_instance_ids_;

  DISALLOW_COPY_AND_ASSIGN(FrameTreeVisualizer);
};

// Uses window.open to open a popup from the frame |opener| with the specified
// |url| and |name|.   Waits for the navigation to |url| to finish and then
// returns the new popup's Shell.  Note that since this navigation to |url| is
// renderer-initiated, it won't cause a process swap unless used in
// --site-per-process mode.
Shell* OpenPopup(const ToRenderFrameHost& opener,
                 const GURL& url,
                 const std::string& name);

// This class can be used to stall any resource request, based on an URL match.
// There is no explicit way to resume the request; it should be used carefully.
// Note: This class likely doesn't work with PlzNavigate.
// TODO(nasko): Reimplement this class using NavigationThrottle, once it has
// the ability to defer navigation requests.
class NavigationStallDelegate : public ResourceDispatcherHostDelegate {
 public:
  explicit NavigationStallDelegate(const GURL& url);

 private:
  // ResourceDispatcherHostDelegate
  void RequestBeginning(
      net::URLRequest* request,
      content::ResourceContext* resource_context,
      content::AppCacheService* appcache_service,
      ResourceType resource_type,
      ScopedVector<content::ResourceThrottle>* throttles) override;

  GURL url_;
};

// This class can be used to pause and resume navigations, based on a URL
// match. Note that it only keeps track of one navigation at a time.
class TestNavigationManager : public WebContentsObserver {
 public:
  // Currently this monitors any frame in WebContents.
  // TODO(clamy): Extend this class so that it can monitor a specific frame.
  TestNavigationManager(WebContents* web_contents, const GURL& url);
  ~TestNavigationManager() override;

  // Waits until the navigation request is ready to be sent to the network
  // stack. The navigation will be paused until it is resumed by calling
  // ResumeNavigation.
  void WaitForWillStartRequest();

  // Resumes the navigation if it was previously paused.
  void ResumeNavigation();

  // Waits until the navigation has been finished. Users of this method should
  // first use WaitForWillStartRequest, then call ResumeNavigation, and only
  // then WaitForNavigationFinished.
  // TODO(clamy): Do not pause the navigation in WillStartRequest by default.
  void WaitForNavigationFinished();

 private:
  // WebContentsObserver implementation.
  void DidStartNavigation(NavigationHandle* handle) override;
  void DidFinishNavigation(NavigationHandle* handle) override;

  // Called when the NavigationThrottle pauses the navigation in
  // WillStartRequest.
  void OnWillStartRequest();

  const GURL url_;
  bool navigation_paused_;
  NavigationHandle* handle_;
  scoped_refptr<MessageLoopRunner> loop_runner_;

  base::WeakPtrFactory<TestNavigationManager> weak_factory_;
};

// Helper class to assist with hit testing surfaces in multiple processes.
// WaitForSurfaceReady() will only return after a Surface from |target_view|
// has been composited in the top-level frame's Surface. At that point,
// browser process hit testing to target_view's Surface can succeed.
class SurfaceHitTestReadyNotifier {
 public:
  SurfaceHitTestReadyNotifier(RenderWidgetHostViewChildFrame* target_view);
  ~SurfaceHitTestReadyNotifier() {}

  void WaitForSurfaceReady();

 private:
  bool ContainsSurfaceId(cc::SurfaceId container_surface_id);

  cc::SurfaceManager* surface_manager_;
  cc::SurfaceId root_surface_id_;
  RenderWidgetHostViewChildFrame* target_view_;

  DISALLOW_COPY_AND_ASSIGN(SurfaceHitTestReadyNotifier);
};

}  // namespace content

#endif  // CONTENT_TEST_CONTENT_BROWSER_TEST_UTILS_INTERNAL_H_
