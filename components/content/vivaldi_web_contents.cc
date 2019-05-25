// Copyright (c) 2018 Vivaldi Technologies AS. All rights reserved.

#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/frame_host/frame_tree_node.h"

#include "app/vivaldi_apptools.h"
#include "content/browser/browser_plugin/browser_plugin_embedder.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/render_widget_host_view_guest.h"
#include "content/browser/web_contents/web_contents_view_child_frame.h"
#include "content/browser/web_contents/web_contents_view_guest.h"
#include "content/public/browser/guest_mode.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents_delegate.h"

namespace content {

void WebContentsImpl::SetExtData(const std::string& ext_data) {
  ext_data_ = ext_data;
  for (auto& observer : observers_)
    observer.ExtDataSet(this);

  NotificationService::current()->Notify(
    NOTIFICATION_EXTDATA_UPDATED,
    Source<WebContents>(this),
    NotificationService::NoDetails());
}

const std::string& WebContentsImpl::GetExtData() const {
  return ext_data_;
}

void FrameTreeNode::DidChangeLoadProgressExtended(double load_progress,
  double loaded_bytes,
  int loaded_elements,
  int total_elements) {
  loaded_bytes_ = loaded_bytes;
  loaded_elements_ = loaded_elements;
  total_elements_ = total_elements;

  frame_tree_->UpdateLoadProgress(load_progress);
}

void WebContentsImpl::FrameTreeNodeDestroyed() {
  for (auto& observer : observers_) {
    observer.WebContentsDidDetach();
  }
}

void WebContentsImpl::AttachedToOuter() {
  for (auto& observer : observers_) {
    observer.WebContentsDidAttach();
  }
}

void WebContentsImpl::WebContentsTreeNode::DetachFromOuterWebContents() {
  if (outer_web_contents_) {
    if ((current_web_contents_->GetDelegate() &&
      !current_web_contents_->GetDelegate()->HasOwnerShipOfContents())) {
      // Detach inner so the WebContents is not destroyed, it is destroyed by
      // the |TabStripModel|
      outer_web_contents_->node_.DetachInnerWebContents(current_web_contents_)
        .release();
    }
  }
  outer_web_contents_ = nullptr;
  outer_contents_frame_tree_node_id_ = FrameTreeNode::kFrameTreeNodeInvalidId;
}

void WebContentsImpl::DetachFromOuter() {
  node_.OnFrameTreeNodeDestroyed(node_.OuterContentsFrameTreeNode());
}

}  // namespace content
