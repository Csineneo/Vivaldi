// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/linux/client_native_pixmap_dmabuf.h"

#include <fcntl.h>
#include <linux/version.h>
#include <stddef.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "base/debug/crash_logging.h"
#include "base/memory/ptr_util.h"
#include "base/process/memory.h"
#include "base/process/process_metrics.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
#include <linux/types.h>

struct local_dma_buf_sync {
  __u64 flags;
};

#define LOCAL_DMA_BUF_SYNC_READ (1 << 0)
#define LOCAL_DMA_BUF_SYNC_WRITE (2 << 0)
#define LOCAL_DMA_BUF_SYNC_RW \
  (LOCAL_DMA_BUF_SYNC_READ | LOCAL_DMA_BUF_SYNC_WRITE)
#define LOCAL_DMA_BUF_SYNC_START (0 << 2)
#define LOCAL_DMA_BUF_SYNC_END (1 << 2)

#define LOCAL_DMA_BUF_BASE 'b'
#define LOCAL_DMA_BUF_IOCTL_SYNC \
  _IOW(LOCAL_DMA_BUF_BASE, 0, struct local_dma_buf_sync)

#else
#include <linux/dma-buf.h>
#endif

namespace gfx {

namespace {

void PrimeSyncStart(int dmabuf_fd) {
  struct local_dma_buf_sync sync_start = {0};

  sync_start.flags = LOCAL_DMA_BUF_SYNC_START | LOCAL_DMA_BUF_SYNC_RW;
#if DCHECK_IS_ON()
  int rv =
#endif
      drmIoctl(dmabuf_fd, LOCAL_DMA_BUF_IOCTL_SYNC, &sync_start);
  DPLOG_IF(ERROR, rv) << "Failed DMA_BUF_SYNC_START";
}

void PrimeSyncEnd(int dmabuf_fd) {
  struct local_dma_buf_sync sync_end = {0};

  sync_end.flags = LOCAL_DMA_BUF_SYNC_END | LOCAL_DMA_BUF_SYNC_RW;
#if DCHECK_IS_ON()
  int rv =
#endif
      drmIoctl(dmabuf_fd, LOCAL_DMA_BUF_IOCTL_SYNC, &sync_end);
  DPLOG_IF(ERROR, rv) << "Failed DMA_BUF_SYNC_END";
}

}  // namespace

// static
std::unique_ptr<gfx::ClientNativePixmap>
ClientNativePixmapDmaBuf::ImportFromDmabuf(
    const gfx::NativePixmapHandle& handle,
    const gfx::Size& size) {
  return base::WrapUnique(new ClientNativePixmapDmaBuf(handle, size));
}

ClientNativePixmapDmaBuf::ClientNativePixmapDmaBuf(
    const gfx::NativePixmapHandle& handle,
    const gfx::Size& size)
    : pixmap_handle_(handle), size_(size), data_{0} {
  TRACE_EVENT0("drm", "ClientNativePixmapDmaBuf");
  // TODO(dcastagna): support multiple fds.
  DCHECK_EQ(1u, handle.fds.size());
  DCHECK_GE(handle.fds.front().fd, 0);
  dmabuf_fd_.reset(handle.fds.front().fd);

  DCHECK_GE(handle.planes.back().size, 0u);
  size_t map_size = handle.planes.back().offset + handle.planes.back().size;
  data_ = mmap(nullptr, map_size, (PROT_READ | PROT_WRITE), MAP_SHARED,
               dmabuf_fd_.get(), 0);
  if (data_ == MAP_FAILED) {
    logging::SystemErrorCode mmap_error = logging::GetLastSystemErrorCode();
    if (mmap_error == ENOMEM)
      base::TerminateBecauseOutOfMemory(map_size);

    // TODO(dcastagna): Remove the following diagnostic information and the
    // associated crash keys once crbug.com/629521 is fixed.
    bool fd_valid = fcntl(dmabuf_fd_.get(), F_GETFD) != -1 ||
                    logging::GetLastSystemErrorCode() != EBADF;
    std::string mmap_params = base::StringPrintf(
        "(addr=nullptr, length=%zu, prot=(PROT_READ | PROT_WRITE), "
        "flags=MAP_SHARED, fd=%d[valid=%d], offset=0)",
        map_size, dmabuf_fd_.get(), fd_valid);
    std::string errno_str = logging::SystemErrorCodeToString(mmap_error);
    std::unique_ptr<base::ProcessMetrics> process_metrics(
        base::ProcessMetrics::CreateCurrentProcessMetrics());
    std::string number_of_fds =
        base::StringPrintf("%d", process_metrics->GetOpenFdCount());
    base::debug::ScopedCrashKey params_crash_key("mmap_params", mmap_params);
    base::debug::ScopedCrashKey size_crash_key("buffer_size", size.ToString());
    base::debug::ScopedCrashKey errno_crash_key("errno", errno_str);
    base::debug::ScopedCrashKey number_of_fds_crash_key("number_of_fds",
                                                        number_of_fds);
    LOG(ERROR) << "Failed to mmap dmabuf; mmap_params: " << mmap_params
               << ", buffer_size: (" << size.ToString()
               << "),  errno: " << errno_str
               << " , number_of_fds: " << number_of_fds;
    CHECK(false) << "Failed to mmap dmabuf.";
  }
}

ClientNativePixmapDmaBuf::~ClientNativePixmapDmaBuf() {
  TRACE_EVENT0("drm", "~ClientNativePixmapDmaBuf");
  size_t map_size =
      pixmap_handle_.planes.back().offset + pixmap_handle_.planes.back().size;
  int ret = munmap(data_, map_size);
  DCHECK(!ret);
}

bool ClientNativePixmapDmaBuf::Map() {
  TRACE_EVENT0("drm", "DmaBuf:Map");
  if (data_ != nullptr) {
    PrimeSyncStart(dmabuf_fd_.get());
    return true;
  }
  return false;
}

void ClientNativePixmapDmaBuf::Unmap() {
  TRACE_EVENT0("drm", "DmaBuf:Unmap");
  PrimeSyncEnd(dmabuf_fd_.get());
}

void* ClientNativePixmapDmaBuf::GetMemoryAddress(size_t plane) const {
  DCHECK_LT(plane, pixmap_handle_.planes.size());
  uint8_t* address = reinterpret_cast<uint8_t*>(data_);
  return address + pixmap_handle_.planes[plane].offset;
}

int ClientNativePixmapDmaBuf::GetStride(size_t plane) const {
  DCHECK_LT(plane, pixmap_handle_.planes.size());
  return pixmap_handle_.planes[plane].stride;
}

}  // namespace gfx
