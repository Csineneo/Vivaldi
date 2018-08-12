// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/RemoteFrameView.h"

#include "core/dom/IntersectionObserverEntry.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/RemoteFrameClient.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutPartItem.h"

namespace blink {

RemoteFrameView::RemoteFrameView(RemoteFrame* remote_frame)
    : remote_frame_(remote_frame) {
  ASSERT(remote_frame);
}

RemoteFrameView::~RemoteFrameView() {}

void RemoteFrameView::SetParent(FrameViewBase* parent) {
  FrameViewBase::SetParent(parent);
  FrameRectsChanged();
}

RemoteFrameView* RemoteFrameView::Create(RemoteFrame* remote_frame) {
  RemoteFrameView* view = new RemoteFrameView(remote_frame);
  view->Show();
  return view;
}

void RemoteFrameView::UpdateRemoteViewportIntersection() {
  if (!remote_frame_->OwnerLayoutObject())
    return;

  FrameView* local_root_view =
      ToLocalFrame(remote_frame_->Tree().Parent())->LocalFrameRoot()->View();
  if (!local_root_view)
    return;

  // Start with rect in remote frame's coordinate space. Then
  // mapToVisualRectInAncestorSpace will move it to the local root's coordinate
  // space and account for any clip from containing elements such as a
  // scrollable div. Passing nullptr as an argument to
  // mapToVisualRectInAncestorSpace causes it to be clipped to the viewport,
  // even if there are RemoteFrame ancestors in the frame tree.
  LayoutRect rect(0, 0, FrameRect().Width(), FrameRect().Height());
  rect.Move(remote_frame_->OwnerLayoutObject()->ContentBoxOffset());
  IntRect viewport_intersection;
  if (remote_frame_->OwnerLayoutObject()->MapToVisualRectInAncestorSpace(
          nullptr, rect)) {
    IntRect root_visible_rect = local_root_view->VisibleContentRect();
    IntRect intersected_rect(rect);
    intersected_rect.Intersect(root_visible_rect);
    intersected_rect.Move(-local_root_view->ScrollOffsetInt());

    // Translate the intersection rect from the root frame's coordinate space
    // to the remote frame's coordinate space.
    viewport_intersection = ConvertFromRootFrame(intersected_rect);
  }

  if (viewport_intersection != last_viewport_intersection_) {
    remote_frame_->Client()->UpdateRemoteViewportIntersection(
        viewport_intersection);
  }

  last_viewport_intersection_ = viewport_intersection;
}

void RemoteFrameView::Dispose() {
  HTMLFrameOwnerElement* owner_element = remote_frame_->DeprecatedLocalOwner();
  // ownerElement can be null during frame swaps, because the
  // RemoteFrameView is disconnected before detachment.
  if (owner_element && owner_element->OwnedWidget() == this)
    owner_element->SetWidget(nullptr);
  FrameViewBase::Dispose();
}

void RemoteFrameView::InvalidateRect(const IntRect& rect) {
  LayoutPartItem layout_item = remote_frame_->OwnerLayoutItem();
  if (layout_item.IsNull())
    return;

  LayoutRect repaint_rect(rect);
  repaint_rect.Move(layout_item.BorderLeft() + layout_item.PaddingLeft(),
                    layout_item.BorderTop() + layout_item.PaddingTop());
  layout_item.InvalidatePaintRectangle(repaint_rect);
}

void RemoteFrameView::SetFrameRect(const IntRect& new_rect) {
  IntRect old_rect = FrameRect();

  if (new_rect == old_rect)
    return;

  FrameViewBase::SetFrameRect(new_rect);

  FrameRectsChanged();
}

void RemoteFrameView::FrameRectsChanged() {
  // Update the rect to reflect the position of the frame relative to the
  // containing local frame root. The position of the local root within
  // any remote frames, if any, is accounted for by the embedder.
  IntRect new_rect = FrameRect();
  if (Parent() && Parent()->IsFrameView())
    new_rect = Parent()->ConvertToRootFrame(
        ToFrameView(Parent())->ContentsToFrame(new_rect));
  remote_frame_->Client()->FrameRectsChanged(new_rect);
}

void RemoteFrameView::Hide() {
  SetSelfVisible(false);

  FrameViewBase::Hide();

  remote_frame_->Client()->VisibilityChanged(false);
}

void RemoteFrameView::Show() {
  SetSelfVisible(true);

  FrameViewBase::Show();

  remote_frame_->Client()->VisibilityChanged(true);
}

void RemoteFrameView::SetParentVisible(bool visible) {
  if (IsParentVisible() == visible)
    return;

  FrameViewBase::SetParentVisible(visible);
  if (!IsSelfVisible())
    return;

  remote_frame_->Client()->VisibilityChanged(IsVisible());
}

DEFINE_TRACE(RemoteFrameView) {
  visitor->Trace(remote_frame_);
  FrameViewBase::Trace(visitor);
}

}  // namespace blink
