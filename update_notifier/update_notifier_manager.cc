// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved

#include "update_notifier/update_notifier_manager.h"

#include <Windows.h>

#include <AclAPI.h>
#include <Psapi.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "app/vivaldi_constants.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_version_info.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/singleton.h"
#include "base/numerics/safe_conversions.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/run_loop.h"
#include "base/strings/string16.h"
#include "base/values.h"
#include "base/win/message_window.h"
#include "base/win/scoped_handle.h"
#include "base/win/win_util.h"
#include "browser/init_sparkle.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_filter.h"
#include "third_party/_winsparkle_lib/include/winsparkle.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "update_notifier/update_notifier_window.h"

namespace ui {
enum ScaleFactor;
}

namespace vivaldi_update_notifier {

namespace {
// This is half of the size we want to use on the first read, because it
// gets multiplied by 2 before being used.
const size_t kInitialSizeOfProcessIdList = 50;

const base::FilePath::StringType kVivaldiExecutable =
    FILE_PATH_LITERAL("vivaldi.exe");

const wchar_t kRestartEventName[] = L"Global\\Vivaldi/Update_notifier/Restart/";
const wchar_t kQuitEventName[] = L"Global\\Vivaldi/Update_notifier/Quit/";

const int kRestartEventActiveSleepInterval = 100;  // ms
const int kQuitAllEventInterval = 1000;            // ms

base::FilePath AddVersionToPathIfNeeded(const base::FilePath& path) {
  if (base::PathExists(path))
    return path;

  base::FilePath dir_exe;
  base::PathService::Get(base::DIR_EXE, &dir_exe);
  base::FilePath version_path =
      dir_exe.Append(UpdateNotifierManager::GetInstance()->current_version());
  if (dir_exe.AppendRelativePath(path, &version_path)) {
    return version_path;
  } else {
    return path;
  }
}

class ResourceBundleDelegate : public ui::ResourceBundle::Delegate {
 public:
  ResourceBundleDelegate() {}
  ~ResourceBundleDelegate() override {}

  base::FilePath GetPathForResourcePack(const base::FilePath& pack_path,
                                        ui::ScaleFactor scale_factor) override {
    return AddVersionToPathIfNeeded(pack_path);
  }

  base::FilePath GetPathForLocalePack(const base::FilePath& pack_path,
                                      const std::string& locale) override {
    if (!pack_path.empty())
      return AddVersionToPathIfNeeded(pack_path);

    // TODO(julienp): This is just ugly. It might be better to just split out
    // most of the update notifier in a dll residing in the versioned folder
    // so that we don't need this workaround.
    base::FilePath new_path;
    base::PathService::Get(base::DIR_EXE, &new_path);
    new_path.Append(UpdateNotifierManager::GetInstance()->current_version());
    new_path.Append(FILE_PATH_LITERAL("locales"));
    new_path.AppendASCII(locale + ".pak");
    if (base::PathExists(new_path))
      return new_path;
    else
      return pack_path;
  }

  gfx::Image GetImageNamed(int resource_id) override { return gfx::Image(); }
  gfx::Image GetNativeImageNamed(int resource_id) override {
    return gfx::Image();
  }

  base::RefCountedMemory* LoadDataResourceBytes(
      int resource_id,
      ui::ScaleFactor scale_factor) override {
    return nullptr;
  }

  bool GetRawDataResource(int resource_id,
                          ui::ScaleFactor scale_factor,
                          base::StringPiece* value) override {
    return false;
  }
  bool GetLocalizedString(int message_id, base::string16* value) override {
    return false;
  }
};

bool IsVivaldiRunning() {
  base::FilePath vivaldi_path;
  base::PathService::Get(base::DIR_EXE, &vivaldi_path);
  vivaldi_path = vivaldi_path.Append(kVivaldiExecutable);

  size_t list_size = kInitialSizeOfProcessIdList;
  size_t size_used = list_size;
  std::vector<DWORD> process_ids;
  while (list_size == size_used) {
    list_size *= 2;  // Try a list twice as big.
    process_ids.resize(list_size);
    DWORD bytes_used;
    if (!EnumProcesses(process_ids.data(),
                       base::checked_cast<DWORD>(process_ids.size() *
                                                 sizeof(process_ids[0])),
                       &bytes_used))
      return false;
    size_used = bytes_used / sizeof(process_ids[0]);
  }

  for (size_t i = 0; i < size_used; ++i) {
    base::win::ScopedHandle process_handle(
        OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_ids[i]));

    if (!process_handle.IsValid())
      continue;

    wchar_t process_exe_path[MAX_PATH];
    DWORD process_eze_path_length = arraysize(process_exe_path);
    QueryFullProcessImageName(process_handle.Get(), 0, process_exe_path,
                              &process_eze_path_length);
    if (base::FilePath::CompareEqualIgnoreCase(vivaldi_path.value(),
                                               process_exe_path))
      return true;
  }

  return false;
}

bool IsVivaldiNotRunning() {
  return !IsVivaldiRunning();
}

bool SafeGetTokenInformation(HANDLE token,
                             TOKEN_INFORMATION_CLASS token_information_class,
                             std::vector<uint8_t>* information) {
  DWORD size;
  information->clear();

  if (GetTokenInformation(token, token_information_class, NULL, 0, &size) ==
          FALSE &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return false;

  information->resize(size);
  return GetTokenInformation(token, token_information_class,
                             information->data(), size, &size) != FALSE;
}

struct ACLDeleter {
  void operator()(ACL* acl) { LocalFree(acl); }
};

bool MakeEventSecurityDescriptor(std::vector<uint8_t>* owner_buffer,
                                 std::vector<uint8_t>* primary_group_buffer,
                                 std::unique_ptr<ACL, ACLDeleter>* dacl,
                                 SECURITY_DESCRIPTOR* security_descriptor) {
  base::win::ScopedHandle process_token([] {
    HANDLE process_token_handle;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token_handle);
    return process_token_handle;
  }());

  if (!process_token.IsValid())
    return false;

  if (!SafeGetTokenInformation(process_token.Get(), TokenOwner, owner_buffer))
    return false;
  if (!SafeGetTokenInformation(process_token.Get(), TokenPrimaryGroup,
                               primary_group_buffer))
    return false;

  uint8_t system_sid[SECURITY_MAX_SID_SIZE];
  DWORD sid_size = sizeof(system_sid);
  CreateWellKnownSid(WinLocalSystemSid, NULL, system_sid, &sid_size);
  uint8_t local_sid[SECURITY_MAX_SID_SIZE];
  sid_size = sizeof(local_sid);
  CreateWellKnownSid(WinLocalSid, NULL, local_sid, &sid_size);
  uint8_t administrators_sid[SECURITY_MAX_SID_SIZE];
  sid_size = sizeof(administrators_sid);
  CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, administrators_sid,
                     &sid_size);

  EXPLICIT_ACCESS explicit_access[3];

  // The SYSTEM user usually has full access to events
  explicit_access[0].grfAccessPermissions = GENERIC_ALL;
  explicit_access[0].grfAccessMode = SET_ACCESS;
  explicit_access[0].grfInheritance = NO_INHERITANCE;
  explicit_access[0].Trustee = {};
  explicit_access[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  explicit_access[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  explicit_access[0].Trustee.ptstrName = reinterpret_cast<wchar_t*>(system_sid);

  // We want any notifiers running as any local user on the machine to
  // be able to listen to this.
  explicit_access[1].grfAccessPermissions = SYNCHRONIZE;
  explicit_access[1].grfAccessMode = SET_ACCESS;
  explicit_access[1].grfInheritance = NO_INHERITANCE;
  explicit_access[1].Trustee = {};
  explicit_access[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  explicit_access[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  explicit_access[1].Trustee.ptstrName = reinterpret_cast<wchar_t*>(local_sid);

  // Vivaldi installers running as an administrator should be able to restart
  // all updaters.
  explicit_access[2].grfAccessPermissions = EVENT_MODIFY_STATE;
  explicit_access[2].grfAccessMode = SET_ACCESS;
  explicit_access[2].grfInheritance = NO_INHERITANCE;
  explicit_access[2].Trustee = {};
  explicit_access[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  explicit_access[2].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  explicit_access[2].Trustee.ptstrName =
      reinterpret_cast<wchar_t*>(administrators_sid);

  ACL* dacl_out;
  SetEntriesInAcl(3, explicit_access, NULL, &dacl_out);
  dacl->reset(dacl_out);

  if (InitializeSecurityDescriptor(security_descriptor,
                                   SECURITY_DESCRIPTOR_REVISION) == FALSE)
    return false;

  if (SetSecurityDescriptorOwner(
          security_descriptor,
          reinterpret_cast<TOKEN_OWNER*>(owner_buffer->data())->Owner,
          TRUE) == FALSE)
    return false;
  if (SetSecurityDescriptorGroup(
          security_descriptor,
          reinterpret_cast<TOKEN_PRIMARY_GROUP*>(primary_group_buffer->data())
              ->PrimaryGroup,
          TRUE) == FALSE)
    return false;
  if (SetSecurityDescriptorDacl(security_descriptor, TRUE, dacl_out, FALSE) ==
      FALSE)
    return false;

  return true;
}

base::WaitableEvent* MakeEvent(const base::string16& event_name) {
  SECURITY_ATTRIBUTES security_attributes;
  security_attributes.bInheritHandle = FALSE;
  security_attributes.nLength = sizeof(security_attributes);

  base::FilePath exe_dir;
  base::PathService::Get(base::DIR_EXE, &exe_dir);
  base::FilePath program_files_dir;
  PathService::Get(base::DIR_PROGRAM_FILES, &program_files_dir);

  // These buffers and the ACL MUST remain in memory until we are done with
  // the
  // security descriptor, because the security descriptor refers to their
  // content.
  std::vector<uint8_t> owner_buffer;
  std::vector<uint8_t> primary_group_buffer;
  std::unique_ptr<ACL, ACLDeleter> dacl;
  SECURITY_DESCRIPTOR security_descriptor;

  if (program_files_dir.IsParent(exe_dir)) {
    // ----System-wide installation----
    // TODO(julienp): This is completely the wrong way to detect a system-wide
    // installation, but it will work with default paths. The installer is
    // currently doing something similar anyway.
    if (MakeEventSecurityDescriptor(&owner_buffer, &primary_group_buffer, &dacl,
                                    &security_descriptor)) {
      security_attributes.lpSecurityDescriptor = &security_descriptor;
    } else {
      // Fallback to using the default descriptor if we failed constructing one.
      security_attributes.lpSecurityDescriptor = NULL;
    }
  } else {
    // On non-system-wide installations, we only need the running user to
    // be able to restart the notifier, so the default descriptor is fine
    security_attributes.lpSecurityDescriptor = NULL;
  }

  base::string16 restart_event_name(event_name);
  base::string16 normalized_path =
      exe_dir.NormalizePathSeparatorsTo(L'/').value();
  // See
  // https://web.archive.org/web/20130528052217/http://blogs.msdn.com/b/michkap/archive/2005/10/17/481600.aspx
  CharUpper(&normalized_path[0]);
  restart_event_name += normalized_path;

  base::win::ScopedHandle event_handle;
  for (int i = 0; i < 3; i++) {
    if (event_handle.IsValid())
      break;
    event_handle.Set(CreateEvent(&security_attributes, TRUE, FALSE,
                                 restart_event_name.c_str()));
    if (event_handle.IsValid())
      break;
    event_handle.Set(OpenEvent(SYNCHRONIZE, FALSE, restart_event_name.c_str()));
  }

  if (event_handle.IsValid())
    return new base::WaitableEvent(std::move(event_handle));
  return nullptr;
}

}  // anonymous namespace

UpdateNotifierManager* UpdateNotifierManager::GetInstance() {
  return base::Singleton<UpdateNotifierManager>::get();
}

UpdateNotifierManager::UpdateNotifierManager() : notification_accepted_(false) {
  restart_event_.reset(MakeEvent(kRestartEventName));
  if (restart_event_.get()) {
    // If the restart event is active at this point, it is probably because it
    // was set, then we restarted and it hasn't been unset yet. Let's just wait
    // it out.
    while (restart_event_->IsSignaled())
      Sleep(kRestartEventActiveSleepInterval);

    restart_event_watch_.StartWatching(
        restart_event_.get(),
        base::Bind(&UpdateNotifierManager::OnEventTriggered,
                   base::Unretained(this)));
  }

  quit_event_.reset(MakeEvent(kQuitEventName));
  if (quit_event_.get()) {
    quit_event_watch_.StartWatching(
        quit_event_.get(), base::Bind(&UpdateNotifierManager::OnEventTriggered,
                                      base::Unretained(this)));
  }

  // Best effort attempt to ensure that only one update notifier is running for
  // a given user, using a local event
  base::win::ScopedHandle quit_all_event_handle;
  quit_all_event_handle.Set(CreateEvent(
      NULL, TRUE, FALSE, vivaldi::kQuitAllUpdateNotifiersEventName));
  if (quit_all_event_handle.IsValid()) {
    quit_all_event_.reset(
        new base::WaitableEvent(std::move(quit_all_event_handle)));

    quit_all_event_->Signal();
    Sleep(kQuitAllEventInterval);
    quit_all_event_->Reset();

    quit_all_event_watch_.StartWatching(
        quit_all_event_.get(),
        base::Bind(&UpdateNotifierManager::OnEventTriggered,
                   base::Unretained(this)));
  }
}

UpdateNotifierManager::~UpdateNotifierManager() {}

/*static*/
bool UpdateNotifierManager::OnUpdateAvailable(const char* version) {
  UpdateNotifierManager* self = GetInstance();

  if (self->notification_accepted_) {
    self->notification_accepted_ = false;
    return true;
  }

  // We make sure to have a copy of version made here by creating a
  // std::string immediately because version itself is deleted immediately
  // after getting out of here.
  self->ui_thread_loop_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&UpdateNotifierWindow::ShowNotification,
                 base::Unretained(self->update_notifier_window_.get()),
                 std::string(version)));

  return false;
}

void UpdateNotifierManager::OnEventTriggered(
    base::WaitableEvent* waitable_event) {
  if (waitable_event == restart_event_.get()) {
    PostQuitMessage(0);
    base::LaunchProcess(*base::CommandLine::ForCurrentProcess(),
                        base::LaunchOptions());
  } else if (waitable_event == quit_event_.get() ||
             waitable_event == quit_all_event_.get()) {
    PostQuitMessage(0);
  } else {
    NOTREACHED();
  }
}

bool UpdateNotifierManager::RunNotifier(HINSTANCE instance) {
  instance_ = instance;

  base::CommandLine::Init(0, nullptr);
  ui_thread_loop_ = base::MessageLoop::current();
  base::RunLoop run_loop;

  std::unique_ptr<FileVersionInfo> file_version_info(
      FileVersionInfo::CreateFileVersionInfoForModule(instance_));
  DCHECK(file_version_info.get());
  current_version_.assign(file_version_info->file_version());

  ui::RegisterPathProvider();
  chrome::RegisterPathProvider();

  l10n_util::OverrideLocaleWithUILanguageList();

  base::FilePath local_state_path;
  CHECK(PathService::Get(chrome::FILE_LOCAL_STATE, &local_state_path));
  scoped_refptr<JsonPrefStore> local_state = new JsonPrefStore(
      local_state_path, ui_thread_loop_->task_runner(), nullptr);
  const base::Value* locale_value;
  std::string locale;
  if (local_state->GetValue(prefs::kApplicationLocale, &locale_value))
    locale_value->GetAsString(&locale);

  ResourceBundleDelegate resource_bundle_delegate;

  const std::string loaded_locale =
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          locale, &resource_bundle_delegate,
          ui::ResourceBundle::LOAD_COMMON_RESOURCES);
  if (loaded_locale.empty())
    return false;

  win_sparkle_set_did_find_update_callback(
      &UpdateNotifierManager::OnUpdateAvailable);
  vivaldi::InitializeSparkle(*base::CommandLine::ForCurrentProcess(),
                             base::Bind(&IsVivaldiNotRunning));

  update_notifier_window_.reset(new UpdateNotifierWindow());

  if (!update_notifier_window_->Init())
    return false;

  run_loop.Run();

  update_notifier_window_.reset();

  return true;
}

void UpdateNotifierManager::TriggerUpdate() {
  notification_accepted_ = true;
  win_sparkle_check_update_without_ui();
}

void UpdateNotifierManager::Disable() {
  base::string16 command;
  base::FilePath exe_path;
  base::PathService::Get(base::FILE_EXE, &exe_path);
  if (base::win::ReadCommandFromAutoRun(
          HKEY_CURRENT_USER, vivaldi::kUpdateNotifierAutorunName, &command) &&
      base::FilePath::CompareEqualIgnoreCase(command, exe_path.value())) {
    base::win::RemoveCommandFromAutoRun(HKEY_CURRENT_USER,
                                        vivaldi::kUpdateNotifierAutorunName);
  }

  PostQuitMessage(0);
}
}  // namespace vivaldi_update_notifier
