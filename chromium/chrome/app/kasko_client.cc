// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "chrome/app/kasko_client.h"

#if BUILDFLAG(ENABLE_KASKO)

#include <windows.h>
#include <stddef.h>

#include <string>
#include <vector>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/process/process_handle.h"
#include "chrome/app/chrome_watcher_client_win.h"
#include "chrome/chrome_watcher/chrome_watcher_main_api.h"
#include "chrome/common/chrome_constants.h"
#include "components/crash/content/app/crashpad.h"
#include "syzygy/kasko/api/client.h"

namespace {

ChromeWatcherClient* g_chrome_watcher_client = nullptr;
kasko::api::MinidumpType g_minidump_type = kasko::api::SMALL_DUMP_TYPE;

base::LazyInstance<std::vector<kasko::api::CrashKey>>::Leaky
    g_kasko_crash_keys = LAZY_INSTANCE_INITIALIZER;

void GetKaskoCrashKeys(const kasko::api::CrashKey** crash_keys,
                       size_t* crash_key_count) {
  crash_reporter::GetCrashKeysForKasko(g_kasko_crash_keys.Pointer());
  *crash_key_count = g_kasko_crash_keys.Pointer()->size();
  *crash_keys = g_kasko_crash_keys.Pointer()->data();
}

}  // namespace

KaskoClient::KaskoClient(ChromeWatcherClient* chrome_watcher_client,
                         kasko::api::MinidumpType minidump_type) {
  DCHECK(!g_chrome_watcher_client);
  g_minidump_type = minidump_type;
  g_chrome_watcher_client = chrome_watcher_client;

  kasko::api::InitializeClient(
      GetKaskoEndpoint(base::GetCurrentProcId()).c_str());

  // Register the crash keys so that they will be available whether a crash
  // report is triggered directly by the browser process or requested by the
  // Chrome Watcher process.
  size_t crash_key_count = 0;
  const kasko::api::CrashKey* crash_keys = nullptr;
  GetKaskoCrashKeys(&crash_keys, &crash_key_count);
  kasko::api::RegisterCrashKeys(crash_keys, crash_key_count);
}

KaskoClient::~KaskoClient() {
  DCHECK(g_chrome_watcher_client);
  g_chrome_watcher_client = nullptr;
  kasko::api::ShutdownClient();
}

// Sends a diagnostic report for the current process, then terminates it.
// |info| is an optional exception record describing an exception on the current
// thread.
// |protobuf| is an optional buffer of length |protobuf_length|.
// |base_addresses| and |lengths| are optional null-terminated arrays of the
// same length. For each entry in |base_addresses|, a memory range starting at
// the specified address and having the length specified in the corresponding
// entry in |lengths| will be explicitly included in the report.
extern "C" void __declspec(dllexport)
    ReportCrashWithProtobufAndMemoryRanges(EXCEPTION_POINTERS* info,
                                           const char* protobuf,
                                           size_t protobuf_length,
                                           const void* const* base_addresses,
                                           const size_t* lengths) {
  if (g_chrome_watcher_client && g_chrome_watcher_client->EnsureInitialized()) {
    size_t crash_key_count = 0;
    const kasko::api::CrashKey* crash_keys = nullptr;
    GetKaskoCrashKeys(&crash_keys, &crash_key_count);
    std::vector<kasko::api::MemoryRange> memory_ranges;
    if (base_addresses && lengths) {
      for (int i = 0; base_addresses[i] != nullptr && lengths[i] != 0; ++i) {
        kasko::api::MemoryRange memory_range = {base_addresses[i], lengths[i]};
        memory_ranges.push_back(memory_range);
      }
    }
    kasko::api::SendReport(info, g_minidump_type, protobuf, protobuf_length,
                           crash_keys, crash_key_count,
                           memory_ranges.size() ? &memory_ranges[0] : nullptr,
                           memory_ranges.size());
  }

  // The Breakpad integration hooks TerminateProcess. Sidestep it to avoid a
  // secondary report. Crashpad, on the other hand, does not hook
  // TerminateProcess so it can be safely invoked.
  // TODO(chrisha): When Breakpad is completely ripped out make this Crashpad
  // specific.
  using TerminateProcessWithoutDumpProc = void(__cdecl*)();
  TerminateProcessWithoutDumpProc terminate_process_without_dump =
      reinterpret_cast<TerminateProcessWithoutDumpProc>(::GetProcAddress(
          ::GetModuleHandle(chrome::kBrowserProcessExecutableName),
          "TerminateProcessWithoutDump"));
  if (terminate_process_without_dump)
    terminate_process_without_dump();
  else
    ::TerminateProcess(::GetCurrentProcess(), 0);
}

extern "C" void __declspec(dllexport) ReportCrashWithProtobuf(
    EXCEPTION_POINTERS* info, const char* protobuf, size_t protobuf_length) {
  ReportCrashWithProtobufAndMemoryRanges(info, protobuf, protobuf_length,
                                         nullptr, nullptr);
}

#endif  // BUILDFLAG(ENABLE_KASKO)
