// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/nacl/renderer/plugin/pnacl_translate_thread.h"

#include <stddef.h>

#include <iterator>
#include <sstream>

#include "base/logging.h"
#include "components/nacl/renderer/plugin/plugin.h"
#include "components/nacl/renderer/plugin/plugin_error.h"
#include "components/nacl/renderer/plugin/temporary_file.h"
#include "components/nacl/renderer/plugin/utility.h"
#include "content/public/common/sandbox_init.h"
#include "native_client/src/shared/platform/nacl_sync_raii.h"
#include "ppapi/c/ppb_file_io.h"
#include "ppapi/cpp/var.h"
#include "ppapi/proxy/ppapi_messages.h"

namespace plugin {
namespace {

template <typename Val>
std::string MakeCommandLineArg(const char* key, const Val val) {
  std::stringstream ss;
  ss << key << val;
  return ss.str();
}

void GetLlcCommandLine(std::vector<std::string>* args,
                       size_t obj_files_size,
                       int32_t opt_level,
                       bool is_debug,
                       const std::string& architecture_attributes) {
  // TODO(dschuff): This CL override is ugly. Change llc to default to
  // using the number of modules specified in the first param, and
  // ignore multiple uses of -split-module
  args->push_back(MakeCommandLineArg("-split-module=", obj_files_size));
  args->push_back(MakeCommandLineArg("-O", opt_level));
  if (is_debug)
    args->push_back("-bitcode-format=llvm");
  if (!architecture_attributes.empty())
    args->push_back("-mattr=" + architecture_attributes);
}

void GetSubzeroCommandLine(std::vector<std::string>* args,
                           int32_t opt_level,
                           bool is_debug,
                           const std::string& architecture_attributes) {
  args->push_back(MakeCommandLineArg("-O", opt_level));
  DCHECK(!is_debug);
  // TODO(stichnot): enable this once the mattr flag formatting is
  // compatible: https://code.google.com/p/nativeclient/issues/detail?id=4132
  // if (!architecture_attributes.empty())
  //   args->push_back("-mattr=" + architecture_attributes);
}

}  // namespace

PnaclTranslateThread::PnaclTranslateThread()
    : compiler_subprocess_(NULL),
      ld_subprocess_(NULL),
      compiler_subprocess_active_(false),
      ld_subprocess_active_(false),
      done_(false),
      compile_time_(0),
      obj_files_(NULL),
      num_threads_(0),
      nexe_file_(NULL),
      coordinator_error_info_(NULL),
      coordinator_(NULL),
      compiler_channel_peer_pid_(base::kNullProcessId),
      ld_channel_peer_pid_(base::kNullProcessId) {
  NaClXMutexCtor(&subprocess_mu_);
  NaClXMutexCtor(&cond_mu_);
  NaClXCondVarCtor(&buffer_cond_);
}

void PnaclTranslateThread::SetupState(
    const pp::CompletionCallback& finish_callback,
    NaClSubprocess* compiler_subprocess,
    NaClSubprocess* ld_subprocess,
    const std::vector<TempFile*>* obj_files,
    int num_threads,
    TempFile* nexe_file,
    ErrorInfo* error_info,
    PP_PNaClOptions* pnacl_options,
    const std::string& architecture_attributes,
    PnaclCoordinator* coordinator) {
  PLUGIN_PRINTF(("PnaclTranslateThread::SetupState)\n"));
  compiler_subprocess_ = compiler_subprocess;
  ld_subprocess_ = ld_subprocess;
  obj_files_ = obj_files;
  num_threads_ = num_threads;
  nexe_file_ = nexe_file;
  coordinator_error_info_ = error_info;
  pnacl_options_ = pnacl_options;
  architecture_attributes_ = architecture_attributes;
  coordinator_ = coordinator;

  report_translate_finished_ = finish_callback;
}

void PnaclTranslateThread::RunCompile(
    const pp::CompletionCallback& compile_finished_callback) {
  PLUGIN_PRINTF(("PnaclTranslateThread::RunCompile)\n"));
  DCHECK(started());
  DCHECK(compiler_subprocess_->service_runtime());
  compiler_subprocess_active_ = true;

  // Take ownership of this IPC channel to make sure that it does not get
  // freed on the child thread when the child thread calls Shutdown().
  compiler_channel_ =
      compiler_subprocess_->service_runtime()->TakeTranslatorChannel();
  // compiler_channel_ is a IPC::SyncChannel, which is not thread-safe and
  // cannot be used directly by the child thread, so create a
  // SyncMessageFilter which can be used by the child thread.
  compiler_channel_filter_ = compiler_channel_->CreateSyncMessageFilter();
  // Make a copy of the process ID, again to avoid any thread-safety issues
  // involved in accessing compiler_subprocess_ on the child thread.
  compiler_channel_peer_pid_ =
      compiler_subprocess_->service_runtime()->get_process_id();

  compile_finished_callback_ = compile_finished_callback;
  translate_thread_.reset(new NaClThread);
  if (translate_thread_ == NULL) {
    TranslateFailed(PP_NACL_ERROR_PNACL_THREAD_CREATE,
                    "could not allocate thread struct.");
    return;
  }
  const int32_t kArbitraryStackSize = 128 * 1024;
  if (!NaClThreadCreateJoinable(translate_thread_.get(), DoCompileThread, this,
                                kArbitraryStackSize)) {
    TranslateFailed(PP_NACL_ERROR_PNACL_THREAD_CREATE,
                    "could not create thread.");
    translate_thread_.reset(NULL);
  }
}

void PnaclTranslateThread::RunLink() {
  PLUGIN_PRINTF(("PnaclTranslateThread::RunLink)\n"));
  DCHECK(started());
  DCHECK(ld_subprocess_->service_runtime());
  ld_subprocess_active_ = true;

  // Take ownership of this IPC channel to make sure that it does not get
  // freed on the child thread when the child thread calls Shutdown().
  ld_channel_ = ld_subprocess_->service_runtime()->TakeTranslatorChannel();
  // ld_channel_ is a IPC::SyncChannel, which is not thread-safe and cannot be
  // used directly by the child thread, so create a SyncMessageFilter which
  // can be used by the child thread.
  ld_channel_filter_ = ld_channel_->CreateSyncMessageFilter();
  // Make a copy of the process ID, again to avoid any thread-safety issues
  // involved in accessing ld_subprocess_ on the child thread.
  ld_channel_peer_pid_ = ld_subprocess_->service_runtime()->get_process_id();

  // Tear down the previous thread.
  // TODO(jvoung): Use base/threading or something where we can have a
  // persistent thread and easily post tasks to that persistent thread.
  NaClThreadJoin(translate_thread_.get());
  translate_thread_.reset(new NaClThread);
  if (translate_thread_ == NULL) {
    TranslateFailed(PP_NACL_ERROR_PNACL_THREAD_CREATE,
                    "could not allocate thread struct.");
    return;
  }
  const int32_t kArbitraryStackSize = 128 * 1024;
  if (!NaClThreadCreateJoinable(translate_thread_.get(), DoLinkThread, this,
                                kArbitraryStackSize)) {
    TranslateFailed(PP_NACL_ERROR_PNACL_THREAD_CREATE,
                    "could not create thread.");
    translate_thread_.reset(NULL);
  }
}

// Called from main thread to send bytes to the translator.
void PnaclTranslateThread::PutBytes(const void* bytes, int32_t count) {
  CHECK(bytes != NULL);
  NaClXMutexLock(&cond_mu_);
  data_buffers_.push_back(std::string());
  data_buffers_.back().insert(data_buffers_.back().end(),
                              static_cast<const char*>(bytes),
                              static_cast<const char*>(bytes) + count);
  NaClXCondVarSignal(&buffer_cond_);
  NaClXMutexUnlock(&cond_mu_);
}

void PnaclTranslateThread::EndStream() {
  NaClXMutexLock(&cond_mu_);
  done_ = true;
  NaClXCondVarSignal(&buffer_cond_);
  NaClXMutexUnlock(&cond_mu_);
}

ppapi::proxy::SerializedHandle PnaclTranslateThread::GetHandleForSubprocess(
    TempFile* file, int32_t open_flags, base::ProcessId peer_pid) {
  IPC::PlatformFileForTransit file_for_transit;

#if defined(OS_WIN)
  if (!content::BrokerDuplicateHandle(
          file->GetFileHandle(),
          peer_pid,
          &file_for_transit,
          0,  // desired_access is 0 since we're using DUPLICATE_SAME_ACCESS.
          DUPLICATE_SAME_ACCESS)) {
    return ppapi::proxy::SerializedHandle();
  }
#else
  file_for_transit = base::FileDescriptor(dup(file->GetFileHandle()), true);
#endif

  // Using 0 disables any use of quota enforcement for this file handle.
  PP_Resource file_io = 0;

  ppapi::proxy::SerializedHandle handle;
  handle.set_file_handle(file_for_transit, open_flags, file_io);
  return handle;
}

void WINAPI PnaclTranslateThread::DoCompileThread(void* arg) {
  PnaclTranslateThread* translator =
      reinterpret_cast<PnaclTranslateThread*>(arg);
  translator->DoCompile();
}

void PnaclTranslateThread::DoCompile() {
  {
    nacl::MutexLocker ml(&subprocess_mu_);
    // If the main thread asked us to exit in between starting the thread
    // and now, just leave now.
    if (!compiler_subprocess_active_)
      return;
  }

  std::vector<ppapi::proxy::SerializedHandle> compiler_output_files;
  for (TempFile* obj_file : *obj_files_) {
    compiler_output_files.push_back(
        GetHandleForSubprocess(obj_file, PP_FILEOPENFLAG_WRITE,
                               compiler_channel_peer_pid_));
  }

  PLUGIN_PRINTF(("DoCompile using subzero: %d\n", pnacl_options_->use_subzero));

  pp::Core* core = pp::Module::Get()->core();
  int64_t do_compile_start_time = NaClGetTimeOfDayMicroseconds();

  std::vector<std::string> args;
  if (pnacl_options_->use_subzero) {
    GetSubzeroCommandLine(&args, pnacl_options_->opt_level,
                          PP_ToBool(pnacl_options_->is_debug),
                          architecture_attributes_);
  } else {
    GetLlcCommandLine(&args, obj_files_->size(),
                      pnacl_options_->opt_level,
                      PP_ToBool(pnacl_options_->is_debug),
                      architecture_attributes_);
  }

  bool success = false;
  std::string error_str;
  if (!compiler_channel_filter_->Send(
      new PpapiMsg_PnaclTranslatorCompileInit(
          num_threads_, compiler_output_files, args, &success, &error_str))) {
    TranslateFailed(PP_NACL_ERROR_PNACL_LLC_INTERNAL,
                    "Compile stream init failed: "
                    "reply not received from PNaCl translator "
                    "(it probably crashed)");
    return;
  }
  if (!success) {
    TranslateFailed(PP_NACL_ERROR_PNACL_LLC_INTERNAL,
                    std::string("Stream init failed: ") + error_str);
    return;
  }
  PLUGIN_PRINTF(("PnaclCoordinator: StreamInit successful\n"));

  // llc process is started.
  while(!done_ || data_buffers_.size() > 0) {
    NaClXMutexLock(&cond_mu_);
    while(!done_ && data_buffers_.size() == 0) {
      NaClXCondVarWait(&buffer_cond_, &cond_mu_);
    }
    PLUGIN_PRINTF(("PnaclTranslateThread awake (done=%d, size=%" NACL_PRIuS
                   ")\n",
                   done_, data_buffers_.size()));
    if (data_buffers_.size() > 0) {
      std::string data;
      data.swap(data_buffers_.front());
      data_buffers_.pop_front();
      NaClXMutexUnlock(&cond_mu_);
      PLUGIN_PRINTF(("StreamChunk\n"));

      if (!compiler_channel_filter_->Send(
              new PpapiMsg_PnaclTranslatorCompileChunk(data, &success))) {
        TranslateFailed(PP_NACL_ERROR_PNACL_LLC_INTERNAL,
                        "Compile stream chunk failed: "
                        "reply not received from PNaCl translator "
                        "(it probably crashed)");
        return;
      }
      if (!success) {
        // If the error was reported by the translator, then we fall through
        // and call PpapiMsg_PnaclTranslatorCompileEnd, which returns a string
        // describing the error, which we can then send to the Javascript
        // console.
        break;
      }
      PLUGIN_PRINTF(("StreamChunk Successful\n"));
      core->CallOnMainThread(
          0,
          coordinator_->GetCompileProgressCallback(data.size()),
          PP_OK);
    } else {
      NaClXMutexUnlock(&cond_mu_);
    }
  }
  PLUGIN_PRINTF(("PnaclTranslateThread done with chunks\n"));
  // Finish llc.
  if (!compiler_channel_filter_->Send(
          new PpapiMsg_PnaclTranslatorCompileEnd(&success, &error_str))) {
    TranslateFailed(PP_NACL_ERROR_PNACL_LLC_INTERNAL,
                    "Compile stream end failed: "
                    "reply not received from PNaCl translator "
                    "(it probably crashed)");
    return;
  }
  if (!success) {
    TranslateFailed(PP_NACL_ERROR_PNACL_LLC_INTERNAL, error_str);
    return;
  }
  compile_time_ = NaClGetTimeOfDayMicroseconds() - do_compile_start_time;
  GetNaClInterface()->LogTranslateTime("NaCl.Perf.PNaClLoadTime.CompileTime",
                                       compile_time_);
  GetNaClInterface()->LogTranslateTime(
      pnacl_options_->use_subzero
          ? "NaCl.Perf.PNaClLoadTime.CompileTime.Subzero"
          : "NaCl.Perf.PNaClLoadTime.CompileTime.LLC",
      compile_time_);

  // Shut down the compiler subprocess.
  NaClXMutexLock(&subprocess_mu_);
  compiler_subprocess_active_ = false;
  compiler_subprocess_->Shutdown();
  NaClXMutexUnlock(&subprocess_mu_);

  core->CallOnMainThread(0, compile_finished_callback_, PP_OK);
}

void WINAPI PnaclTranslateThread::DoLinkThread(void* arg) {
  PnaclTranslateThread* translator =
      reinterpret_cast<PnaclTranslateThread*>(arg);
  translator->DoLink();
}

void PnaclTranslateThread::DoLink() {
  {
    nacl::MutexLocker ml(&subprocess_mu_);
    // If the main thread asked us to exit in between starting the thread
    // and now, just leave now.
    if (!ld_subprocess_active_)
      return;
  }

  // Reset object files for reading first.  We do this before duplicating
  // handles/FDs to prevent any handle/FD leaks in case any of the Reset()
  // calls fail.
  for (TempFile* obj_file : *obj_files_) {
    if (!obj_file->Reset()) {
      TranslateFailed(PP_NACL_ERROR_PNACL_LD_SETUP,
                      "Link process could not reset object file");
      return;
    }
  }

  ppapi::proxy::SerializedHandle nexe_file =
      GetHandleForSubprocess(nexe_file_, PP_FILEOPENFLAG_WRITE,
                             ld_channel_peer_pid_);
  std::vector<ppapi::proxy::SerializedHandle> ld_input_files;
  for (TempFile* obj_file : *obj_files_) {
    ld_input_files.push_back(
        GetHandleForSubprocess(obj_file, PP_FILEOPENFLAG_READ,
                               ld_channel_peer_pid_));
  }

  int64_t link_start_time = NaClGetTimeOfDayMicroseconds();
  bool success = false;
  bool sent = ld_channel_filter_->Send(
      new PpapiMsg_PnaclTranslatorLink(ld_input_files, nexe_file, &success));
  if (!sent) {
    TranslateFailed(PP_NACL_ERROR_PNACL_LD_INTERNAL,
                    "link failed: reply not received from linker.");
    return;
  }
  if (!success) {
    TranslateFailed(PP_NACL_ERROR_PNACL_LD_INTERNAL,
                    "link failed: linker returned failure status.");
    return;
  }

  GetNaClInterface()->LogTranslateTime(
      "NaCl.Perf.PNaClLoadTime.LinkTime",
      NaClGetTimeOfDayMicroseconds() - link_start_time);
  PLUGIN_PRINTF(("PnaclCoordinator: link (translator=%p) succeeded\n",
                 this));

  // Shut down the ld subprocess.
  NaClXMutexLock(&subprocess_mu_);
  ld_subprocess_active_ = false;
  ld_subprocess_->Shutdown();
  NaClXMutexUnlock(&subprocess_mu_);

  pp::Core* core = pp::Module::Get()->core();
  core->CallOnMainThread(0, report_translate_finished_, PP_OK);
}

void PnaclTranslateThread::TranslateFailed(
    PP_NaClError err_code,
    const std::string& error_string) {
  PLUGIN_PRINTF(("PnaclTranslateThread::TranslateFailed (error_string='%s')\n",
                 error_string.c_str()));
  pp::Core* core = pp::Module::Get()->core();
  if (coordinator_error_info_->message().empty()) {
    // Only use our message if one hasn't already been set by the coordinator
    // (e.g. pexe load failed).
    coordinator_error_info_->SetReport(err_code,
                                       std::string("PnaclCoordinator: ") +
                                       error_string);
  }
  core->CallOnMainThread(0, report_translate_finished_, PP_ERROR_FAILED);
}

void PnaclTranslateThread::AbortSubprocesses() {
  PLUGIN_PRINTF(("PnaclTranslateThread::AbortSubprocesses\n"));
  NaClXMutexLock(&subprocess_mu_);
  if (compiler_subprocess_ != NULL && compiler_subprocess_active_) {
    // We only run the service_runtime's Shutdown and do not run the
    // NaClSubprocess Shutdown, which would otherwise nullify some
    // pointers that could still be in use (srpc_client, etc.).
    compiler_subprocess_->service_runtime()->Shutdown();
    compiler_subprocess_active_ = false;
  }
  if (ld_subprocess_ != NULL && ld_subprocess_active_) {
    ld_subprocess_->service_runtime()->Shutdown();
    ld_subprocess_active_ = false;
  }
  NaClXMutexUnlock(&subprocess_mu_);
  nacl::MutexLocker ml(&cond_mu_);
  done_ = true;
  // Free all buffered bitcode chunks
  data_buffers_.clear();
  NaClXCondVarSignal(&buffer_cond_);
}

PnaclTranslateThread::~PnaclTranslateThread() {
  PLUGIN_PRINTF(("~PnaclTranslateThread (translate_thread=%p)\n", this));
  AbortSubprocesses();
  if (translate_thread_ != NULL)
    NaClThreadJoin(translate_thread_.get());
  PLUGIN_PRINTF(("~PnaclTranslateThread joined\n"));
  NaClCondVarDtor(&buffer_cond_);
  NaClMutexDtor(&cond_mu_);
  NaClMutexDtor(&subprocess_mu_);
}

} // namespace plugin
