// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/client/app/blimp_startup.h"

#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "blimp/client/app/blimp_discardable_memory_allocator.h"
#include "blimp/client/feature/compositor/decoding_image_generator.h"
#include "third_party/skia/include/core/SkGraphics.h"
#include "ui/gl/gl_surface.h"

class SkImageGenerator;

namespace {
base::LazyInstance<scoped_ptr<base::MessageLoopForUI>> g_main_message_loop =
    LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<blimp::client::BlimpDiscardableMemoryAllocator>
    g_discardable_memory_allocator = LAZY_INSTANCE_INITIALIZER;

SkImageGenerator* CreateImageGenerator(SkData* data) {
  return blimp::client::DecodingImageGenerator::create(data);
}

}  // namespace

namespace blimp {
namespace client {

void InitializeLogging() {
  logging::LoggingSettings settings;
#if defined(OS_ANDROID)
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
#else
  base::FilePath log_filename;
  PathService::Get(base::DIR_EXE, &log_filename);
  log_filename = log_filename.AppendASCII("blimp_client.log");
  settings.logging_dest = logging::LOG_TO_ALL;
  settings.log_file = log_filename.value().c_str();
  settings.delete_old = logging::DELETE_OLD_LOG_FILE;
#endif  // OS_ANDROID
  logging::InitLogging(settings);
  logging::SetLogItems(false,   // Process ID
                       false,   // Thread ID
                       false,   // Timestamp
                       false);  // Tick count
  VLOG(0) << "Chromium logging enabled: level = " << logging::GetMinLogLevel()
          << ", default verbosity = " << logging::GetVlogVerbosity();
}

bool InitializeMainMessageLoop() {
  // TODO(dtrainor): Initialize ICU?

  // Set the DiscardableMemoryAllocator.
  base::DiscardableMemoryAllocator::SetInstance(
      g_discardable_memory_allocator.Pointer());
  if (!gfx::GLSurface::InitializeOneOff())
    return false;
  SkGraphics::Init();
  SkGraphics::SetImageGeneratorFromEncodedFactory(CreateImageGenerator);
  g_main_message_loop.Get().reset(new base::MessageLoopForUI);
  return true;
}

}  // namespace client
}  // namespace blimp
