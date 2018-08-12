// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/overflow_bubble.h"

#include "ash/shelf/overflow_bubble_view.h"
#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_view.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "ash/system/tray/tray_background_view.h"
#include "ui/events/event.h"
#include "ui/views/widget/widget.h"

namespace ash {

OverflowBubble::OverflowBubble()
    : bubble_(NULL),
      anchor_(NULL),
      shelf_view_(NULL) {
  Shell::GetInstance()->AddPointerWatcher(this);
}

OverflowBubble::~OverflowBubble() {
  Hide();
  Shell::GetInstance()->RemovePointerWatcher(this);
}

void OverflowBubble::Show(views::View* anchor, ShelfView* shelf_view) {
  Hide();

  bubble_ = new OverflowBubbleView();
  bubble_->InitOverflowBubble(anchor, shelf_view);
  shelf_view_ = shelf_view;
  anchor_ = anchor;

  TrayBackgroundView::InitializeBubbleAnimations(bubble_->GetWidget());
  bubble_->GetWidget()->AddObserver(this);
  bubble_->GetWidget()->Show();
}

void OverflowBubble::Hide() {
  if (!IsShowing())
    return;

  bubble_->GetWidget()->RemoveObserver(this);
  bubble_->GetWidget()->Close();
  bubble_ = NULL;
  anchor_ = NULL;
  shelf_view_ = NULL;
}

void OverflowBubble::HideBubbleAndRefreshButton() {
  if (!IsShowing())
    return;

  views::View* anchor = anchor_;
  Hide();
  // Update overflow button (|anchor|) status when overflow bubble is hidden
  // by outside event of overflow button.
  anchor->SchedulePaint();
}

void OverflowBubble::ProcessPressedEvent(
    const gfx::Point& event_location_in_screen) {
  if (IsShowing() && !shelf_view_->IsShowingMenu() &&
      !bubble_->GetBoundsInScreen().Contains(event_location_in_screen) &&
      !anchor_->GetBoundsInScreen().Contains(event_location_in_screen)) {
    HideBubbleAndRefreshButton();
  }
}

void OverflowBubble::OnMousePressed(const ui::MouseEvent& event,
                                    const gfx::Point& location_in_screen,
                                    views::Widget* target) {
  ProcessPressedEvent(location_in_screen);
}

void OverflowBubble::OnTouchPressed(const ui::TouchEvent& event,
                                    const gfx::Point& location_in_screen,
                                    views::Widget* target) {
  ProcessPressedEvent(location_in_screen);
}

void OverflowBubble::OnWidgetDestroying(views::Widget* widget) {
  DCHECK(widget == bubble_->GetWidget());
  bubble_ = NULL;
  anchor_ = NULL;
  shelf_view_->shelf()->SchedulePaint();
  shelf_view_ = NULL;
}

}  // namespace ash
