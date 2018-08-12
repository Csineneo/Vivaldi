// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_UI_SHIM_H_
#define CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_UI_SHIM_H_

// This class lives on the UI thread and supports classes like the
// BackingStoreProxy, which must live on the UI thread. The IO thread
// portion of this class, the GpuProcessHost, is responsible for
// shuttling messages between the browser and GPU processes.

#include <stdint.h>

#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "gpu/config/gpu_info.h"
#include "gpu/ipc/common/memory_stats.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"
#include "ipc/message_router.h"

namespace IPC {
class Message;
}

namespace service_manager {
class BinderRegistry;
}

namespace content {
void RouteToGpuProcessHostUIShimTask(int host_id, const IPC::Message& msg);

class GpuProcessHostUIShim : public base::NonThreadSafe {
 public:
  // Create a GpuProcessHostUIShim with the given ID.  The object can be found
  // using FromID with the same id.
  static GpuProcessHostUIShim* Create(int host_id);

  // Destroy the GpuProcessHostUIShim with the given host ID. This can only
  // be called on the UI thread. Only the GpuProcessHost should destroy the
  // UI shim.
  static void Destroy(int host_id, const std::string& message);

  CONTENT_EXPORT static GpuProcessHostUIShim* FromID(int host_id);

  // The GpuProcessHost causes this to be called on the UI thread to
  // dispatch the incoming messages from the GPU process, which are
  // actually received on the IO thread.
  void OnMessageReceived(const IPC::Message& message);

  // Register Mojo interfaces that must be bound on the UI thread.
  static void RegisterUIThreadMojoInterfaces(
      service_manager::BinderRegistry* registry);

 private:
  explicit GpuProcessHostUIShim(int host_id);
  ~GpuProcessHostUIShim();

  // The serial number of the GpuProcessHost / GpuProcessHostUIShim pair.
  int host_id_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_UI_SHIM_H_
