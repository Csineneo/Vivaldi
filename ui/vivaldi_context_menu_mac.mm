//
// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved.
//
// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/vivaldi_context_menu_mac.h"

#include "base/compiler_specific.h"
#import "base/mac/scoped_sending_event.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu.h"
#include "components/renderer_context_menu/render_view_context_menu_base.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/models/simple_menu_model.h"
#import "ui/base/cocoa/menu_controller.h"
#include "ui/views/vivaldi_bookmark_menu_views.h"

using content::WebContents;

namespace {

// Retrieves an NSMenuItem which has the specified command_id. This function
// traverses the given |model| in the depth-first order. When this function
// finds an item whose command_id is the same as the given |command_id|, it
// returns the NSMenuItem associated with the item. This function emulates
// views::MenuItemViews::GetMenuItemByID() for Mac.
NSMenuItem* VivaldiGetMenuItemByID(ui::MenuModel* model,
                            NSMenu* menu,
                            int command_id) {
  for (int i = 0; i < model->GetItemCount(); ++i) {
    NSMenuItem* item = [menu itemAtIndex:i];
    if (model->GetCommandIdAt(i) == command_id)
      return item;

    ui::MenuModel* submenu = model->GetSubmenuModelAt(i);
    if (submenu && [item hasSubmenu]) {
      NSMenuItem* subitem = VivaldiGetMenuItemByID(submenu,
                                            [item submenu],
                                            command_id);
      if (subitem)
        return subitem;
    }
  }
  return nil;
}

}  // namespace

namespace vivaldi {
VivaldiContextMenu* CreateVivaldiContextMenu(
    content::WebContents* web_contents,
    ui::SimpleMenuModel* menu_model,
    const content::ContextMenuParams& params) {
  return new VivaldiContextMenuMac(web_contents, menu_model, params);
}

VivaldiBookmarkMenu* CreateVivaldiBookmarkMenu(
    content::WebContents* web_contents,
    const vivaldi::BookmarkMenuParams& params,
    const gfx::Rect& button_rect) {
  // Transform coordinate from local to screen. Since Mac uses a lower-left
  // origin and chrome does not we must adjust for this before and after,
  NSView* view = web_contents->GetNativeView().GetNativeNSView();
  NSWindow* window = [view window];
  // Adjust position to lower left in view
  NSPoint position = NSMakePoint(button_rect.x(),
                                 NSHeight([view bounds]) - button_rect.y());
  // Position in window
  position = [view convertPoint:position toView:nil];
  // Position in screen
  NSRect screen_rect = [window convertRectToScreen:
      NSMakeRect(position.x, position.y, 0, 0)];
  // Adjust to upper left in screen
  gfx::Rect rect(screen_rect.origin.x,
                 window.screen.frame.size.height - screen_rect.origin.y,
                 button_rect.width(),
                 button_rect.height());

  return new VivaldiBookmarkMenuViews(web_contents, params, rect);
}
}  // vivialdi

VivaldiContextMenuMac::VivaldiContextMenuMac(
    content::WebContents* web_contents,
    ui::SimpleMenuModel* menu_model,
    const content::ContextMenuParams& params)
  :web_contents_(web_contents),
   menu_model_(menu_model),
   params_(params) {
}

VivaldiContextMenuMac::~VivaldiContextMenuMac() {
}

void VivaldiContextMenuMac::Show() {
  NSView* parent_view = GetActiveNativeView();
  if (!parent_view)
    return;

  menu_controller_.reset(
      [[MenuControllerCocoa alloc] initWithModel:menu_model_
                     useWithPopUpButtonCell:NO]);

  // Synthesize an event for the click, as there is no certainty that
  // [NSApp currentEvent] will return a valid event.
  gfx::Point params_position(params_.x, params_.y);
  NSEvent* currentEvent = [NSApp currentEvent];
  NSWindow* window = [parent_view window];
  NSPoint position =
      NSMakePoint(params_position.x(),
                  NSHeight([parent_view bounds]) - params_position.y());
  position = [parent_view convertPoint:position toView:nil];
  NSTimeInterval eventTime = [currentEvent timestamp];
  NSEvent* clickEvent = [NSEvent mouseEventWithType:NSRightMouseDown
                                           location:position
                                      modifierFlags:NSRightMouseDownMask
                                          timestamp:eventTime
                                       windowNumber:[window windowNumber]
                                            context:nil
                                        eventNumber:0
                                         clickCount:1
                                           pressure:1.0];

  {
    // Make sure events can be pumped while the menu is up.
    base::MessageLoopCurrent::ScopedNestableTaskAllower allow;

    // One of the events that could be pumped is |window.close()|.
    // User-initiated event-tracking loops protect against this by
    // setting flags in -[CrApplication sendEvent:], but since
    // web-content menus are initiated by IPC message the setup has to
    // be done manually.
    base::mac::ScopedSendingEvent sendingEventScoper;

    // Show the menu.
    [NSMenu popUpContextMenu:[menu_controller_ menu]
                   withEvent:clickEvent
                     forView:parent_view];
  }
}

void VivaldiContextMenuMac::SetIcon(const gfx::Image& icon, int id) {
  NSMenuItem* item = VivaldiGetMenuItemByID(menu_model_, [menu_controller_ menu], id);
  item.image = icon.ToNSImage();
}

NSView* VivaldiContextMenuMac::GetActiveNativeView() {
  return web_contents_->GetNativeView().GetNativeNSView();
}
