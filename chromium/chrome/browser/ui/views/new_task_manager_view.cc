// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/new_task_manager_view.h"

#include <stddef.h>

#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/task_management/task_manager_observer.h"
#include "chrome/browser/task_manager/task_manager.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/task_manager/task_manager_columns.h"
#include "chrome/browser/ui/user_manager.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/table_model_observer.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/table/table_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_client_view.h"

#if defined(USE_ASH)
#include "ash/shelf/shelf_util.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "grit/ash_resources.h"
#endif  // defined(USE_ASH)

#if defined(OS_WIN)
#include "chrome/browser/shell_integration_win.h"
#include "ui/base/win/shell.h"
#include "ui/views/win/hwnd_util.h"
#endif  // defined(OS_WIN)

namespace task_management {

namespace {

NewTaskManagerView* g_task_manager_view = nullptr;

}  // namespace

NewTaskManagerView::~NewTaskManagerView() {
  // Delete child views now, while our table model still exists.
  RemoveAllChildViews(true);
}

// static
task_management::TaskManagerTableModel* NewTaskManagerView::Show(
    Browser* browser) {
  if (g_task_manager_view) {
    // If there's a Task manager window open already, just activate it.
    g_task_manager_view->GetWidget()->Activate();
    return g_task_manager_view->table_model_.get();
  }

  g_task_manager_view = new NewTaskManagerView();

  gfx::NativeWindow window = browser ? browser->window()->GetNativeWindow()
                                     : nullptr;
#if defined(USE_ASH)
  // NOTE(jarle@vivaldi): Do not call ash::wm::GetActiveWindow unless we have a 
  // valid Shell instance, otherwise it will terminate the process via
  // Shell::GetPrimaryRootWindow. [VB-10963]
  if (!window && ash::Shell::HasInstance())
    window = ash::wm::GetActiveWindow();
#endif

  DialogDelegate::CreateDialogWidget(g_task_manager_view,
                                     window,
                                     nullptr);
  g_task_manager_view->InitAlwaysOnTopState();

#if defined(OS_WIN)
  // Set the app id for the task manager to the app id of its parent browser. If
  // no parent is specified, the app id will default to that of the initial
  // process.
  if (browser) {
    ui::win::SetAppIdForWindow(
        shell_integration::win::GetChromiumModelIdForProfile(
            browser->profile()->GetPath()),
        views::HWNDForWidget(g_task_manager_view->GetWidget()));
  }
#endif

  g_task_manager_view->GetWidget()->Show();

  // Set the initial focus to the list of tasks.
  views::FocusManager* focus_manager =
      g_task_manager_view->GetFocusManager();
  if (focus_manager)
    focus_manager->SetFocusedView(g_task_manager_view->tab_table_);

#if defined(USE_ASH)
  gfx::NativeWindow native_window =
      g_task_manager_view->GetWidget()->GetNativeWindow();
  ash::SetShelfItemDetailsForDialogWindow(native_window,
                                          IDR_ASH_SHELF_ICON_TASK_MANAGER,
                                          native_window->title());
#endif
  return g_task_manager_view->table_model_.get();
}

// static
void NewTaskManagerView::Hide() {
  if (g_task_manager_view)
    g_task_manager_view->GetWidget()->Close();
}

bool NewTaskManagerView::IsColumnVisible(int column_id) const {
  return tab_table_->IsColumnVisible(column_id);
}

void NewTaskManagerView::SetColumnVisibility(int column_id,
                                             bool new_visibility) {
  tab_table_->SetColumnVisibility(column_id, new_visibility);
}

bool NewTaskManagerView::IsTableSorted() const {
  return tab_table_->is_sorted();
}

TableSortDescriptor
NewTaskManagerView::GetSortDescriptor() const {
  if (!IsTableSorted())
    return TableSortDescriptor();

  const auto& descriptor = tab_table_->sort_descriptors().front();
  return TableSortDescriptor(descriptor.column_id, descriptor.ascending);
}

void NewTaskManagerView::ToggleSortOrder(int visible_column_index) {
  tab_table_->ToggleSortOrder(visible_column_index);
}

gfx::Size NewTaskManagerView::GetPreferredSize() const {
  return gfx::Size(460, 270);
}

bool NewTaskManagerView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  DCHECK_EQ(ui::VKEY_W, accelerator.key_code());
  DCHECK_EQ(ui::EF_CONTROL_DOWN, accelerator.modifiers());
  GetWidget()->Close();
  return true;
}

bool NewTaskManagerView::CanResize() const {
  return true;
}

bool NewTaskManagerView::CanMaximize() const {
  return true;
}

bool NewTaskManagerView::CanMinimize() const {
  return true;
}

bool NewTaskManagerView::ExecuteWindowsCommand(int command_id) {
  return false;
}

base::string16 NewTaskManagerView::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_TASK_MANAGER_TITLE);
}

std::string NewTaskManagerView::GetWindowName() const {
  return prefs::kTaskManagerWindowPlacement;
}

bool NewTaskManagerView::Accept() {
  using SelectedIndices = ui::ListSelectionModel::SelectedIndices;
  SelectedIndices selection(tab_table_->selection_model().selected_indices());
  for (SelectedIndices::const_reverse_iterator i = selection.rbegin();
       i != selection.rend(); ++i) {
    table_model_->KillTask(*i);
  }

  // Just kill the process, don't close the task manager dialog.
  return false;
}

bool NewTaskManagerView::Close() {
  return true;
}

int NewTaskManagerView::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_OK;
}

base::string16 NewTaskManagerView::GetDialogButtonLabel(
    ui::DialogButton button) const {
  return l10n_util::GetStringUTF16(IDS_TASK_MANAGER_KILL);
}

bool NewTaskManagerView::IsDialogButtonEnabled(ui::DialogButton button) const {
  const ui::ListSelectionModel::SelectedIndices& selections(
      tab_table_->selection_model().selected_indices());
  for (const auto& selection : selections) {
    if (table_model_->IsBrowserProcess(selection))
      return false;
  }

  return !selections.empty() && TaskManager::IsEndProcessEnabled();
}

void NewTaskManagerView::WindowClosing() {
  // Now that the window is closed, we can allow a new one to be opened.
  // (WindowClosing comes in asynchronously from the call to Close() and we
  // may have already opened a new instance).
  if (g_task_manager_view == this) {
    // We don't have to delete |g_task_manager_view| as we don't own it. It's
    // owned by the Views hierarchy.
    g_task_manager_view = nullptr;
  }
  table_model_->StoreColumnsSettings();
}

bool NewTaskManagerView::ShouldUseCustomFrame() const {
  return false;
}

void NewTaskManagerView::GetGroupRange(int model_index,
                                       views::GroupRange* range) {
  table_model_->GetRowsGroupRange(model_index, &range->start, &range->length);
}

void NewTaskManagerView::OnSelectionChanged() {
  GetDialogClientView()->UpdateDialogButtons();
}

void NewTaskManagerView::OnDoubleClick() {
  ActivateFocusedTab();
}

void NewTaskManagerView::OnKeyDown(ui::KeyboardCode keycode) {
  if (keycode == ui::VKEY_RETURN)
    ActivateFocusedTab();
}

void NewTaskManagerView::ShowContextMenuForView(
    views::View* source,
    const gfx::Point& point,
    ui::MenuSourceType source_type) {
  ui::SimpleMenuModel menu_model(this);

  for (const auto& table_column : columns_) {
    menu_model.AddCheckItem(table_column.id,
                            l10n_util::GetStringUTF16(table_column.id));
  }

  menu_runner_.reset(
      new views::MenuRunner(&menu_model, views::MenuRunner::CONTEXT_MENU));

  if (menu_runner_->RunMenuAt(GetWidget(),
                              nullptr,
                              gfx::Rect(point, gfx::Size()),
                              views::MENU_ANCHOR_TOPLEFT,
                              source_type) == views::MenuRunner::MENU_DELETED) {
    return;
  }
}

bool NewTaskManagerView::IsCommandIdChecked(int id) const {
  return tab_table_->IsColumnVisible(id);
}

bool NewTaskManagerView::IsCommandIdEnabled(int id) const {
  return true;
}

bool NewTaskManagerView::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) {
  return false;
}

void NewTaskManagerView::ExecuteCommand(int id, int event_flags) {
  table_model_->ToggleColumnVisibility(id);
}

NewTaskManagerView::NewTaskManagerView()
    : tab_table_(nullptr),
      tab_table_parent_(nullptr),
      is_always_on_top_(false) {
  Init();
}

// static
NewTaskManagerView* NewTaskManagerView::GetInstanceForTests() {
  return g_task_manager_view;
}

void NewTaskManagerView::Init() {
  // Create the table columns.
  for (size_t i = 0; i < kColumnsSize; ++i) {
    const auto& col_data = kColumns[i];
    columns_.push_back(ui::TableColumn(col_data.id, col_data.align,
                                       col_data.width, col_data.percent));
    columns_.back().sortable = col_data.sortable;
    columns_.back().initial_sort_is_ascending =
        col_data.initial_sort_is_ascending;
  }

  // Create the table view.
  tab_table_ = new views::TableView(nullptr, columns_, views::ICON_AND_TEXT,
                                    false);
  table_model_.reset(new TaskManagerTableModel(REFRESH_TYPE_CPU |
                                               REFRESH_TYPE_MEMORY |
                                               REFRESH_TYPE_NETWORK_USAGE,
                                               this));
  tab_table_->SetModel(table_model_.get());
  tab_table_->SetGrouper(this);
  tab_table_->SetObserver(this);
  tab_table_->set_context_menu_controller(this);
  set_context_menu_controller(this);

  tab_table_parent_ = tab_table_->CreateParentIfNecessary();
  AddChildView(tab_table_parent_);

  SetLayoutManager(new views::FillLayout());
  SetBorder(views::Border::CreateEmptyBorder(views::kPanelVertMargin,
                                             views::kButtonHEdgeMarginNew, 0,
                                             views::kButtonHEdgeMarginNew));

  table_model_->RetrieveSavedColumnsSettingsAndUpdateTable();

  AddAccelerator(ui::Accelerator(ui::VKEY_W, ui::EF_CONTROL_DOWN));
}

void NewTaskManagerView::InitAlwaysOnTopState() {
  RetrieveSavedAlwaysOnTopState();
  GetWidget()->SetAlwaysOnTop(is_always_on_top_);
}

void NewTaskManagerView::ActivateFocusedTab() {
  const int active_row = tab_table_->selection_model().active();
  if (active_row != ui::ListSelectionModel::kUnselectedIndex)
    table_model_->ActivateTask(active_row);
}

void NewTaskManagerView::RetrieveSavedAlwaysOnTopState() {
  is_always_on_top_ = false;

  if (!g_browser_process->local_state())
    return;

  const base::DictionaryValue* dictionary =
    g_browser_process->local_state()->GetDictionary(GetWindowName());
  if (dictionary)
    dictionary->GetBoolean("always_on_top", &is_always_on_top_);
}

}  // namespace task_management
