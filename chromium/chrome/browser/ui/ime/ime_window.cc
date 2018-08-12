// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ime/ime_window.h"

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ime/ime_native_window.h"
#include "chrome/browser/ui/ime/ime_window_observer.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/screen.h"

namespace {

// The vertical margin between the cursor and the follow-cursor window.
const int kFollowCursorMargin = 3;

}  // namespace

namespace ui {

ImeWindow::ImeWindow(Profile* profile,
                     const extensions::Extension* extension,
                     const std::string& url,
                     Mode mode,
                     const gfx::Rect& bounds)
    : mode_(mode), native_window_(nullptr) {
  if (extension) {  // Allow nullable |extension| for testability.
    title_ = extension->name();
    icon_.reset(new extensions::IconImage(
        profile, extension, extensions::IconsInfo::GetIcons(extension),
        extension_misc::EXTENSION_ICON_SMALL, gfx::ImageSkia(), this));
  }

  registrar_.Add(this, chrome::NOTIFICATION_APP_TERMINATING,
                 content::NotificationService::AllSources());

  GURL gurl(url);
  if (!gurl.is_valid())
    gurl = extension->GetResourceURL(url);

  content::SiteInstance* instance =
      content::SiteInstance::CreateForURL(profile, gurl);
  content::WebContents::CreateParams create_params(profile, instance);
  web_contents_.reset(content::WebContents::Create(create_params));
  web_contents_->SetDelegate(this);
  content::OpenURLParams params(gurl, content::Referrer(), SINGLETON_TAB,
                                ui::PAGE_TRANSITION_LINK, false);
  web_contents_->OpenURL(params);

  native_window_ = CreateNativeWindow(this, bounds, web_contents_.get());
}

void ImeWindow::Show() {
  native_window_->Show();
}

void ImeWindow::Hide() {
  native_window_->Hide();
}

void ImeWindow::Close() {
  native_window_->Close();
}

void ImeWindow::SetBounds(const gfx::Rect& bounds) {
  native_window_->SetBounds(bounds);
}

void ImeWindow::FollowCursor(const gfx::Rect& cursor_bounds) {
  if (mode_ != FOLLOW_CURSOR)
    return;

  gfx::Rect screen_bounds =
      gfx::Screen::GetScreen()->GetPrimaryDisplay().bounds();
  gfx::Rect window_bounds = native_window_->GetBounds();
  int screen_width = screen_bounds.width();
  int screen_height = screen_bounds.height();
  int width = window_bounds.width();
  int height = window_bounds.height();
  // By default, aligns the left of the window to the left of the cursor, and
  // aligns the top of the window to the bottom of the cursor.
  // If the right of the window would go beyond the screen bounds, aligns the
  // right of the window to the screen bounds.
  // If the bottom of the window would go beyond the screen bounds, aligns the
  // bottom of the window to the cursor top.
  int x = cursor_bounds.x();
  int y = cursor_bounds.y() + cursor_bounds.height() + kFollowCursorMargin;
  if (width < screen_width && x + width > screen_width)
    x = screen_width - width;
  if (height < screen_height && y + height > screen_height)
    y = cursor_bounds.y() - height - kFollowCursorMargin;
  window_bounds.set_x(x);
  window_bounds.set_y(y);
  SetBounds(window_bounds);
}

int ImeWindow::GetFrameId() const {
  return web_contents_->GetMainFrame()->GetRoutingID();
}

void ImeWindow::OnWindowDestroyed() {
  FOR_EACH_OBSERVER(ImeWindowObserver, observers_, OnWindowDestroyed(this));
  native_window_ = nullptr;
  delete this;
  // TODO(shuchen): manages the ime window instances in ImeWindowManager.
  // e.g. for normal window, limits the max window count, and for follow cursor
  // window, limits single window instance (and reuse).
  // So at here it will callback to ImeWindowManager to delete |this|.
}

void ImeWindow::AddObserver(ImeWindowObserver* observer) {
  observers_.AddObserver(observer);
}

void ImeWindow::OnExtensionIconImageChanged(extensions::IconImage* image) {
  if (native_window_)
    native_window_->UpdateWindowIcon();
}

ImeWindow::~ImeWindow() {}

void ImeWindow::Observe(int type,
                        const content::NotificationSource& source,
                        const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_APP_TERMINATING)
    Close();
}

content::WebContents* ImeWindow::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params) {
  source->GetController().LoadURL(params.url, params.referrer,
                                  params.transition, params.extra_headers);
  return source;
}

bool ImeWindow::CanDragEnter(
    content::WebContents* source,
    const content::DropData& data,
    blink::WebDragOperationsMask operations_allowed) {
  return false;
}

void ImeWindow::CloseContents(content::WebContents* source) {
  Close();
}

void ImeWindow::MoveContents(content::WebContents* source,
                                 const gfx::Rect& pos) {
  if (native_window_)
    native_window_->SetBounds(pos);
}

bool ImeWindow::IsPopupOrPanel(const content::WebContents* source) const {
  return true;
}

}  // namespace ui
