// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/runner/host/child_process_host.h"

#include <stdint.h>

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/public/cpp/bindings/interface_ptr_info.h"
#include "mojo/public/cpp/system/core.h"
#include "mojo/shell/native_runner_delegate.h"
#include "mojo/shell/runner/common/switches.h"

#if defined(OS_LINUX) && !defined(OS_ANDROID)
#include "sandbox/linux/services/namespace_sandbox.h"
#endif

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace mojo {
namespace shell {

ChildProcessHost::ChildProcessHost(base::TaskRunner* launch_process_runner,
                                   NativeRunnerDelegate* delegate,
                                   bool start_sandboxed,
                                   const Identity& target,
                                   const base::FilePath& app_path)
    : launch_process_runner_(launch_process_runner),
      delegate_(delegate),
      start_sandboxed_(start_sandboxed),
      target_(target),
      app_path_(app_path),
      start_child_process_event_(false, false),
      weak_factory_(this) {
  node_channel_.reset(new edk::PlatformChannelPair);
  primordial_pipe_token_ = edk::GenerateRandomToken();
  controller_.Bind(
      InterfacePtrInfo<mojom::ChildController>(
          edk::CreateParentMessagePipe(primordial_pipe_token_), 0u));
}

ChildProcessHost::ChildProcessHost(ScopedHandle channel)
    : launch_process_runner_(nullptr),
      delegate_(nullptr),
      start_sandboxed_(false),
      start_child_process_event_(false, false),
      weak_factory_(this) {
  CHECK(channel.is_valid());
  ScopedMessagePipeHandle handle(MessagePipeHandle(channel.release().value()));
  controller_.Bind(
      InterfacePtrInfo<mojom::ChildController>(std::move(handle), 0u));
}

ChildProcessHost::~ChildProcessHost() {
  if (!app_path_.empty())
    CHECK(!controller_) << "Destroying ChildProcessHost before calling Join";
}

void ChildProcessHost::Start(const ProcessReadyCallback& callback) {
  DCHECK(!child_process_.IsValid());
  launch_process_runner_->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&ChildProcessHost::DoLaunch, base::Unretained(this)),
      base::Bind(&ChildProcessHost::DidStart,
                 weak_factory_.GetWeakPtr(), callback));
}

int ChildProcessHost::Join() {
  if (controller_)  // We use this as a signal that Start was called.
    start_child_process_event_.Wait();

  controller_ = mojom::ChildControllerPtr();
  // This host may be hosting a child process whose lifetime is controlled
  // elsewhere. In this case we have no known process handle to wait on.
  if (child_process_.IsValid()) {
    int rv = -1;
    LOG_IF(ERROR, !child_process_.WaitForExit(&rv))
        << "Failed to wait for child process";

    child_process_.Close();
    return rv;
  }

  return 0;
}

void ChildProcessHost::StartApp(
    InterfaceRequest<mojom::ShellClient> request,
    const mojom::ChildController::StartAppCallback& on_app_complete) {
  DCHECK(controller_);

  // In this case the process must have already been launched.
  start_child_process_event_.Signal();

  on_app_complete_ = on_app_complete;
  controller_->StartApp(
      std::move(request),
      base::Bind(&ChildProcessHost::AppCompleted, weak_factory_.GetWeakPtr()));
}

void ChildProcessHost::ExitNow(int32_t exit_code) {
  DCHECK(controller_);

  controller_->ExitNow(exit_code);
}

void ChildProcessHost::DidStart(const ProcessReadyCallback& callback) {
  DVLOG(2) << "ChildProcessHost::DidStart()";

  if (child_process_.IsValid()) {
    callback.Run(child_process_.Pid());
  } else {
    LOG(ERROR) << "Failed to start child process";
    AppCompleted(MOJO_RESULT_UNKNOWN);
  }
}

void ChildProcessHost::DoLaunch() {
  const base::CommandLine* parent_command_line =
      base::CommandLine::ForCurrentProcess();
  base::FilePath target_path = parent_command_line->GetProgram();
  // |app_path_| can be empty in tests.
  if (!app_path_.MatchesExtension(FILE_PATH_LITERAL(".mojo")) &&
      !app_path_.empty()) {
    target_path = app_path_;
  }

  base::CommandLine child_command_line(target_path);
  child_command_line.AppendArguments(*parent_command_line, false);

  if (target_path != app_path_)
    child_command_line.AppendSwitchPath(switches::kChildProcess, app_path_);

  if (start_sandboxed_)
    child_command_line.AppendSwitch(switches::kEnableSandbox);

  if (node_channel_.get()) {
    node_channel_->PrepareToPassClientHandleToChildProcess(
        &child_command_line, &handle_passing_info_);
  }

  child_command_line.AppendSwitchASCII(switches::kPrimordialPipeToken,
                                       primordial_pipe_token_);

  if (delegate_) {
    delegate_->AdjustCommandLineArgumentsForTarget(target_,
                                                   &child_command_line);
  }

  base::LaunchOptions options;
#if defined(OS_WIN)
  if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
    options.handles_to_inherit = &handle_passing_info_;
  } else {
#if defined(OFFICIAL_BUILD)
    CHECK(false) << "Launching mojo process with inherit_handles is insecure!";
#endif
    options.inherit_handles = true;
  }
  options.stdin_handle = INVALID_HANDLE_VALUE;
  options.stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  options.stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
  // Always inherit stdout/stderr as a pair.
  if (!options.stdout_handle || !options.stdin_handle)
    options.stdin_handle = options.stdout_handle = nullptr;

  // Pseudo handles are used when stdout and stderr redirect to the console. In
  // that case, they're automatically inherited by child processes. See
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms682075.aspx
  // Trying to add them to the list of handles to inherit causes CreateProcess
  // to fail. When this process is launched from Python
  // (i.e. by apptest_runner.py) then a real handle is used. In that case, we do
  // want to add it to the list of handles that is inherited.
  if (options.stdout_handle &&
      GetFileType(options.stdout_handle) != FILE_TYPE_CHAR) {
    handle_passing_info_.push_back(options.stdout_handle);
  }
  if (options.stderr_handle &&
      GetFileType(options.stderr_handle) != FILE_TYPE_CHAR &&
      options.stdout_handle != options.stderr_handle) {
    handle_passing_info_.push_back(options.stderr_handle);
  }
#elif defined(OS_POSIX)
  handle_passing_info_.push_back(std::make_pair(STDIN_FILENO, STDIN_FILENO));
  handle_passing_info_.push_back(std::make_pair(STDOUT_FILENO, STDOUT_FILENO));
  handle_passing_info_.push_back(std::make_pair(STDERR_FILENO, STDERR_FILENO));
  options.fds_to_remap = &handle_passing_info_;
#endif
  DVLOG(2) << "Launching child with command line: "
           << child_command_line.GetCommandLineString();
#if defined(OS_LINUX) && !defined(OS_ANDROID)
  if (start_sandboxed_) {
    child_process_ =
        sandbox::NamespaceSandbox::LaunchProcess(child_command_line, options);
    if (!child_process_.IsValid()) {
      LOG(ERROR) << "Starting the process with a sandbox failed. Missing kernel"
                 << " support.";
    }
  } else
#endif
    child_process_ = base::LaunchProcess(child_command_line, options);

  if (child_process_.IsValid()) {
    platform_channel_pair_.ChildProcessLaunched();
    if (node_channel_.get()) {
      node_channel_->ChildProcessLaunched();
      mojo::edk::ChildProcessLaunched(
          child_process_.Handle(),
          mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(
              node_channel_->PassServerHandle().release().handle)));
    }
  }
  start_child_process_event_.Signal();
}

void ChildProcessHost::AppCompleted(int32_t result) {
  if (!on_app_complete_.is_null()) {
    auto on_app_complete = on_app_complete_;
    on_app_complete_.reset();
    on_app_complete.Run(result);
  }
}

}  // namespace shell
}  // namespace mojo
