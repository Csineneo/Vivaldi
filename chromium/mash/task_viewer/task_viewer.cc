// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mash/task_viewer/task_viewer.h"

#include <stddef.h>
#include <stdint.h>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/package_manager/public/interfaces/catalog.mojom.h"
#include "mojo/shell/public/cpp/connection.h"
#include "mojo/shell/public/cpp/connector.h"
#include "mojo/shell/public/interfaces/application_manager.mojom.h"
#include "ui/base/models/table_model.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/table/table_view.h"
#include "ui/views/controls/table/table_view_observer.h"
#include "ui/views/mus/aura_init.h"
#include "ui/views/mus/window_manager_connection.h"
#include "ui/views/widget/widget_delegate.h"

namespace mash {
namespace task_viewer {
namespace {

using ListenerRequest =
    mojo::InterfaceRequest<mojo::shell::mojom::ApplicationManagerListener>;
using mojo::shell::mojom::ApplicationInfoPtr;

class TaskViewerContents
    : public views::WidgetDelegateView,
      public ui::TableModel,
      public views::ButtonListener,
      public mojo::shell::mojom::ApplicationManagerListener {
 public:
  TaskViewerContents(ListenerRequest request,
                     package_manager::mojom::CatalogPtr catalog)
      : binding_(this, std::move(request)),
        catalog_(std::move(catalog)),
        table_view_(nullptr),
        table_view_parent_(nullptr),
        kill_button_(
            new views::LabelButton(this, base::ASCIIToUTF16("Kill Process"))),
        observer_(nullptr),
        weak_ptr_factory_(this) {
    // We don't want to show an empty UI on startup, so just block until we
    // receive the initial set of applications.
    binding_.WaitForIncomingMethodCall();

    table_view_ = new views::TableView(this, GetColumns(), views::TEXT_ONLY,
                                       false);
    set_background(views::Background::CreateStandardPanelBackground());

    table_view_parent_ = table_view_->CreateParentIfNecessary();
    AddChildView(table_view_parent_);

    kill_button_->SetStyle(views::Button::STYLE_BUTTON);
    AddChildView(kill_button_);
  }
  ~TaskViewerContents() override {
    table_view_->SetModel(nullptr);
    base::MessageLoop::current()->QuitWhenIdle();
  }

 private:
  struct InstanceInfo {
    InstanceInfo(uint32_t id,
                 const std::string& url,
                 base::ProcessId pid)
        : id(id), url(url), pid(pid) {}
    uint32_t id;
    std::string url;
    uint32_t pid;
    std::string name;
  };


  // Overridden from views::WidgetDelegate:
  views::View* GetContentsView() override { return this; }
  base::string16 GetWindowTitle() const override {
    // TODO(beng): use resources.
    return base::ASCIIToUTF16("Tasks");
  }

  // Overridden from views::View:
  void Layout() override {
    gfx::Rect bounds = GetLocalBounds();
    bounds.Inset(10, 10);

    gfx::Size ps = kill_button_->GetPreferredSize();
    bounds.set_height(bounds.height() - ps.height() - 10);

    kill_button_->SetBounds(bounds.width() - ps.width(),
                            bounds.bottom() + 10,
                            ps.width(), ps.height());
    table_view_parent_->SetBoundsRect(bounds);
  }

  // Overridden from ui::TableModel:
  int RowCount() override {
    return static_cast<int>(instances_.size());
  }
  base::string16 GetText(int row, int column_id) override {
    switch(column_id) {
    case 0:
      DCHECK(row < static_cast<int>(instances_.size()));
      return base::UTF8ToUTF16(instances_[row]->name);
    case 1:
      DCHECK(row < static_cast<int>(instances_.size()));
      return base::UTF8ToUTF16(instances_[row]->url);
    case 2:
      DCHECK(row < static_cast<int>(instances_.size()));
      return base::IntToString16(instances_[row]->pid);
    default:
      NOTREACHED();
      break;
    }
    return base::string16();
  }
  void SetObserver(ui::TableModelObserver* observer) override {
    observer_ = observer;
  }

  // Overridden from views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override {
    DCHECK_EQ(sender, kill_button_);
    DCHECK_EQ(table_view_->SelectedRowCount(), 1);
    int row = table_view_->FirstSelectedRow();
    DCHECK(row < static_cast<int>(instances_.size()));
    base::Process process = base::Process::Open(instances_[row]->pid);
    process.Terminate(9, true);
  }

  // Overridden from mojo::shell::mojom::ApplicationManagerListener:
  void SetRunningApplications(
      mojo::Array<ApplicationInfoPtr> applications) override {
    // This callback should only be called with an empty model.
    DCHECK(instances_.empty());
    mojo::Array<mojo::String> urls;
    for (size_t i = 0; i < applications.size(); ++i) {
      InsertInstance(applications[i]->id, applications[i]->url,
                     applications[i]->pid);
      urls.push_back(applications[i]->url);
    }
    catalog_->GetEntries(std::move(urls),
                         base::Bind(&TaskViewerContents::OnGotCatalogEntries,
                                    weak_ptr_factory_.GetWeakPtr()));
  }
  void ApplicationInstanceCreated(ApplicationInfoPtr application) override {
    DCHECK(!ContainsId(application->id));
    InsertInstance(application->id, application->url, application->pid);
    observer_->OnItemsAdded(static_cast<int>(instances_.size()), 1);
    mojo::Array<mojo::String> urls;
    urls.push_back(application->url);
    catalog_->GetEntries(std::move(urls),
                         base::Bind(&TaskViewerContents::OnGotCatalogEntries,
                                    weak_ptr_factory_.GetWeakPtr()));
  }
  void ApplicationInstanceDestroyed(uint32_t id) override {
    for (auto it = instances_.begin(); it != instances_.end(); ++it) {
      if ((*it)->id == id) {
        observer_->OnItemsRemoved(
            static_cast<int>(it - instances_.begin()), 1);
        instances_.erase(it);
        return;
      }
    }
    NOTREACHED();
  }
  void ApplicationPIDAvailable(uint32_t id, uint32_t pid) override {
    for (auto it = instances_.begin(); it != instances_.end(); ++it) {
      if ((*it)->id == id) {
        (*it)->pid = pid;
        observer_->OnItemsChanged(
            static_cast<int>(it - instances_.begin()), 1);
        return;
      }
    }
  }

  bool ContainsId(uint32_t id) const {
    for (auto& it : instances_) {
      if (it->id == id)
        return true;
    }
    return false;
  }

  void InsertInstance(uint32_t id, const std::string& url, uint32_t pid) {
    instances_.push_back(make_scoped_ptr(new InstanceInfo(id, url, pid)));
  }

  void OnGotCatalogEntries(
      mojo::Map<mojo::String,
                package_manager::mojom::CatalogEntryPtr> entries) {
    for (auto it = instances_.begin(); it != instances_.end(); ++it) {
      auto entry_it = entries.find((*it)->url);
      if (entry_it != entries.end()) {
        (*it)->name = entry_it->second->name;
        observer_->OnItemsChanged(
            static_cast<int>(it - instances_.begin()), 1);
      }
    }
  }

  static std::vector<ui::TableColumn> GetColumns() {
    std::vector<ui::TableColumn> columns;

    ui::TableColumn name_column;
    name_column.id = 0;
    // TODO(beng): use resources.
    name_column.title = base::ASCIIToUTF16("Name");
    name_column.width = -1;
    name_column.percent = 0.4f;
    name_column.sortable = true;
    columns.push_back(name_column);

    ui::TableColumn url_column;
    url_column.id = 1;
    // TODO(beng): use resources.
    url_column.title = base::ASCIIToUTF16("URL");
    url_column.width = -1;
    url_column.percent = 0.4f;
    url_column.sortable = true;
    columns.push_back(url_column);

    ui::TableColumn pid_column;
    pid_column.id = 2;
    // TODO(beng): use resources.
    pid_column.title  = base::ASCIIToUTF16("PID");
    pid_column.width = 50;
    pid_column.sortable = true;
    columns.push_back(pid_column);

    return columns;
  }

  mojo::Binding<mojo::shell::mojom::ApplicationManagerListener> binding_;
  package_manager::mojom::CatalogPtr catalog_;

  views::TableView* table_view_;
  views::View* table_view_parent_;
  views::LabelButton* kill_button_;
  ui::TableModelObserver* observer_;

  std::vector<scoped_ptr<InstanceInfo>> instances_;

  base::WeakPtrFactory<TaskViewerContents> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(TaskViewerContents);
};

}  // namespace

TaskViewer::TaskViewer() {}
TaskViewer::~TaskViewer() {}

void TaskViewer::Initialize(mojo::Connector* connector,
                            const std::string& url,
                            uint32_t id, uint32_t user_id) {
  tracing_.Initialize(connector, url);

  aura_init_.reset(new views::AuraInit(connector, "views_mus_resources.pak"));
  views::WindowManagerConnection::Create(connector);

  mojo::shell::mojom::ApplicationManagerPtr application_manager;
  connector->ConnectToInterface("mojo:shell", &application_manager);

  mojo::shell::mojom::ApplicationManagerListenerPtr listener;
  ListenerRequest request = GetProxy(&listener);
  application_manager->AddListener(std::move(listener));

  package_manager::mojom::CatalogPtr catalog;
  connector->ConnectToInterface("mojo:package_manager", &catalog);

  TaskViewerContents* task_viewer = new TaskViewerContents(
      std::move(request), std::move(catalog));
  views::Widget* window = views::Widget::CreateWindowWithBounds(
      task_viewer, gfx::Rect(10, 10, 500, 500));
  window->Show();
}

}  // namespace task_viewer
}  // namespace main
