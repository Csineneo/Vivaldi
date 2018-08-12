// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_context_menu/mock_render_view_context_menu.h"

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/renderer_context_menu/render_view_context_menu_observer.h"
#include "content/public/browser/browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

MockRenderViewContextMenu::MockMenuItem::MockMenuItem()
    : command_id(0), enabled(false), checked(false), hidden(true) {}

MockRenderViewContextMenu::MockMenuItem::~MockMenuItem() {}

MockRenderViewContextMenu::MockRenderViewContextMenu(bool incognito)
    : observer_(nullptr),
      original_profile_(TestingProfile::Builder().Build()),
      profile_(incognito ? original_profile_->GetOffTheRecordProfile()
                         : original_profile_.get()) {}

MockRenderViewContextMenu::~MockRenderViewContextMenu() {}

bool MockRenderViewContextMenu::IsCommandIdChecked(int command_id) const {
  return observer_->IsCommandIdChecked(command_id);
}

bool MockRenderViewContextMenu::IsCommandIdEnabled(int command_id) const {
  return observer_->IsCommandIdEnabled(command_id);
}

void MockRenderViewContextMenu::ExecuteCommand(int command_id,
                                               int event_flags) {
  observer_->ExecuteCommand(command_id);
}

void MockRenderViewContextMenu::MenuWillShow(ui::SimpleMenuModel* source) {}

void MockRenderViewContextMenu::MenuClosed(ui::SimpleMenuModel* source) {}

bool MockRenderViewContextMenu::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) {
  return false;
}

void MockRenderViewContextMenu::AddMenuItem(int command_id,
                                            const base::string16& title) {
  MockMenuItem item;
  item.command_id = command_id;
  item.enabled = observer_->IsCommandIdEnabled(command_id);
  item.checked = false;
  item.hidden = false;
  item.title = title;
  items_.push_back(item);
}

void MockRenderViewContextMenu::AddCheckItem(int command_id,
                                             const base::string16& title) {
  MockMenuItem item;
  item.command_id = command_id;
  item.enabled = observer_->IsCommandIdEnabled(command_id);
  item.checked = observer_->IsCommandIdChecked(command_id);
  item.hidden = false;
  item.title = title;
  items_.push_back(item);
}

void MockRenderViewContextMenu::AddSeparator() {
  MockMenuItem item;
  item.command_id = -1;
  item.enabled = false;
  item.checked = false;
  item.hidden = false;
  items_.push_back(item);
}

void MockRenderViewContextMenu::AddSubMenu(int command_id,
                                           const base::string16& label,
                                           ui::MenuModel* model) {
  MockMenuItem item;
  item.command_id = -1;
  item.enabled = false;
  item.checked = false;
  item.hidden = false;
  items_.push_back(item);
}

void MockRenderViewContextMenu::UpdateMenuItem(int command_id,
                                               bool enabled,
                                               bool hidden,
                                               const base::string16& title) {
  for (auto& item : items_) {
    if (item.command_id == command_id) {
      item.enabled = enabled;
      item.hidden = hidden;
      item.title = title;
      return;
    }
  }

  FAIL() << "Menu observer is trying to change a menu item it doesn't own.";
}

void MockRenderViewContextMenu::AddSpellCheckServiceItem(bool is_checked) {
  AddCheckItem(
      IDC_CONTENT_CONTEXT_SPELLING_TOGGLE,
      l10n_util::GetStringUTF16(IDS_CONTENT_CONTEXT_SPELLING_ASK_GOOGLE));
}

content::RenderViewHost* MockRenderViewContextMenu::GetRenderViewHost() const {
  return nullptr;
}

content::BrowserContext* MockRenderViewContextMenu::GetBrowserContext() const {
  return profile_;
}

content::WebContents* MockRenderViewContextMenu::GetWebContents() const {
  return nullptr;
}

void MockRenderViewContextMenu::SetObserver(
    RenderViewContextMenuObserver* observer) {
  observer_ = observer;
}

size_t MockRenderViewContextMenu::GetMenuSize() const {
  return items_.size();
}

bool MockRenderViewContextMenu::GetMenuItem(size_t index,
                                            MockMenuItem* item) const {
  if (index >= items_.size())
    return false;
  item->command_id = items_[index].command_id;
  item->enabled = items_[index].enabled;
  item->checked = items_[index].checked;
  item->hidden = items_[index].hidden;
  item->title = items_[index].title;
  return true;
}

PrefService* MockRenderViewContextMenu::GetPrefs() {
  return profile_->GetPrefs();
}
