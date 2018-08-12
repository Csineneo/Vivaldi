// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/filesystem/file_impl.h"

#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "components/filesystem/lock_table.h"
#include "components/filesystem/util.h"
#include "mojo/common/common_type_converters.h"
#include "mojo/platform_handle/platform_handle_functions.h"

static_assert(sizeof(off_t) <= sizeof(int64_t), "off_t too big");
static_assert(sizeof(size_t) >= sizeof(uint32_t), "size_t too small");

using base::Time;
using mojo::ScopedHandle;

namespace filesystem {
namespace {

const size_t kMaxReadSize = 1 * 1024 * 1024;  // 1 MB.

}  // namespace

FileImpl::FileImpl(mojo::InterfaceRequest<File> request,
                   const base::FilePath& path,
                   uint32_t flags,
                   LockTable* lock_table)
    : binding_(this, std::move(request)),
      file_(path, flags),
      path_(path),
      lock_table_(lock_table) {
  DCHECK(file_.IsValid());
}

FileImpl::FileImpl(mojo::InterfaceRequest<File> request,
                   const base::FilePath& path,
                   base::File file,
                   LockTable* lock_table)
    : binding_(this, std::move(request)),
      file_(std::move(file)),
      path_(path),
      lock_table_(lock_table) {
  DCHECK(file_.IsValid());
}

FileImpl::~FileImpl() {
  if (file_.IsValid())
    lock_table_->RemoveFromLockTable(path_);
}

bool FileImpl::IsValid() const {
  return file_.IsValid();
}

base::File::Error FileImpl::RawLockFile() {
  return file_.Lock();
}

base::File::Error FileImpl::RawUnlockFile() {
  return file_.Unlock();
}

void FileImpl::Close(const CloseCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_));
    return;
  }

  lock_table_->RemoveFromLockTable(path_);
  file_.Close();
  callback.Run(FileError::OK);
}

// TODO(vtl): Move the implementation to a thread pool.
void FileImpl::Read(uint32_t num_bytes_to_read,
                    int64_t offset,
                    Whence whence,
                    const ReadCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_), mojo::Array<uint8_t>());
    return;
  }
  if (num_bytes_to_read > kMaxReadSize) {
    callback.Run(FileError::INVALID_OPERATION, mojo::Array<uint8_t>());
    return;
  }
  FileError error = IsOffsetValid(offset);
  if (error != FileError::OK) {
    callback.Run(error, mojo::Array<uint8_t>());
    return;
  }
  error = IsWhenceValid(whence);
  if (error != FileError::OK) {
    callback.Run(error, mojo::Array<uint8_t>());
    return;
  }

  if (file_.Seek(static_cast<base::File::Whence>(whence), offset) == -1) {
    callback.Run(FileError::FAILED, mojo::Array<uint8_t>());
    return;
  }

  mojo::Array<uint8_t> bytes_read(num_bytes_to_read);
  int num_bytes_read = file_.ReadAtCurrentPos(
      reinterpret_cast<char*>(&bytes_read.front()), num_bytes_to_read);
  if (num_bytes_read < 0) {
    callback.Run(FileError::FAILED, mojo::Array<uint8_t>());
    return;
  }

  DCHECK_LE(static_cast<size_t>(num_bytes_read), num_bytes_to_read);
  bytes_read.resize(static_cast<size_t>(num_bytes_read));
  callback.Run(FileError::OK, std::move(bytes_read));
}

// TODO(vtl): Move the implementation to a thread pool.
void FileImpl::Write(mojo::Array<uint8_t> bytes_to_write,
                     int64_t offset,
                     Whence whence,
                     const WriteCallback& callback) {
  DCHECK(!bytes_to_write.is_null());
  if (!file_.IsValid()) {
    callback.Run(GetError(file_), 0);
    return;
  }
  // Who knows what |write()| would return if the size is that big (and it
  // actually wrote that much).
  if (bytes_to_write.size() >
#if defined(OS_WIN)
      static_cast<size_t>(std::numeric_limits<int>::max())) {
#else
      static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
#endif
    callback.Run(FileError::INVALID_OPERATION, 0);
    return;
  }
  FileError error = IsOffsetValid(offset);
  if (error != FileError::OK) {
    callback.Run(error, 0);
    return;
  }
  error = IsWhenceValid(whence);
  if (error != FileError::OK) {
    callback.Run(error, 0);
    return;
  }

  if (file_.Seek(static_cast<base::File::Whence>(whence), offset) == -1) {
    callback.Run(FileError::FAILED, 0);
    return;
  }

  const char* buf = (bytes_to_write.size() > 0)
                        ? reinterpret_cast<char*>(&bytes_to_write.front())
                        : nullptr;
  int num_bytes_written = file_.WriteAtCurrentPos(
      buf, static_cast<int>(bytes_to_write.size()));
  if (num_bytes_written < 0) {
    callback.Run(FileError::FAILED, 0);
    return;
  }

  DCHECK_LE(static_cast<size_t>(num_bytes_written),
            std::numeric_limits<uint32_t>::max());
  callback.Run(FileError::OK, static_cast<uint32_t>(num_bytes_written));
}

void FileImpl::Tell(const TellCallback& callback) {
  Seek(0, Whence::FROM_CURRENT, callback);
}

void FileImpl::Seek(int64_t offset,
                    Whence whence,
                    const SeekCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_), 0);
    return;
  }
  FileError error = IsOffsetValid(offset);
  if (error != FileError::OK) {
    callback.Run(error, 0);
    return;
  }
  error = IsWhenceValid(whence);
  if (error != FileError::OK) {
    callback.Run(error, 0);
    return;
  }

  int64_t position =
      file_.Seek(static_cast<base::File::Whence>(whence), offset);
  if (position < 0) {
    callback.Run(FileError::FAILED, 0);
    return;
  }

  callback.Run(FileError::OK, static_cast<int64_t>(position));
}

void FileImpl::Stat(const StatCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_), nullptr);
    return;
  }

  base::File::Info info;
  if (!file_.GetInfo(&info)) {
    callback.Run(FileError::FAILED, nullptr);
    return;
  }

  callback.Run(FileError::OK, MakeFileInformation(info));
}

void FileImpl::Truncate(int64_t size, const TruncateCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_));
    return;
  }
  if (size < 0) {
    callback.Run(FileError::INVALID_OPERATION);
    return;
  }
  FileError error = IsOffsetValid(size);
  if (error != FileError::OK) {
    callback.Run(error);
    return;
  }

  if (!file_.SetLength(size)) {
    callback.Run(FileError::NOT_FOUND);
    return;
  }

  callback.Run(FileError::OK);
}

void FileImpl::Touch(TimespecOrNowPtr atime,
                     TimespecOrNowPtr mtime,
                     const TouchCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_));
    return;
  }

  base::Time base_atime = Time::Now();
  if (!atime) {
    base::File::Info info;
    if (!file_.GetInfo(&info)) {
      callback.Run(FileError::FAILED);
      return;
    }

    base_atime = info.last_accessed;
  } else if (!atime->now) {
    base_atime = Time::FromDoubleT(atime->seconds);
  }

  base::Time base_mtime = Time::Now();
  if (!mtime) {
    base::File::Info info;
    if (!file_.GetInfo(&info)) {
      callback.Run(FileError::FAILED);
      return;
    }

    base_mtime = info.last_modified;
  } else if (!mtime->now) {
    base_mtime = Time::FromDoubleT(mtime->seconds);
  }

  file_.SetTimes(base_atime, base_mtime);
  callback.Run(FileError::OK);
}

void FileImpl::Dup(mojo::InterfaceRequest<File> file,
                   const DupCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_));
    return;
  }

  base::File new_file = file_.Duplicate();
  if (!new_file.IsValid()) {
    callback.Run(GetError(new_file));
    return;
  }

  if (file.is_pending())
    new FileImpl(std::move(file), path_, std::move(new_file), lock_table_);
  callback.Run(FileError::OK);
}

void FileImpl::Flush(const FlushCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_));
    return;
  }

  bool ret = file_.Flush();
  callback.Run(ret ? FileError::OK : FileError::FAILED);
}

void FileImpl::Lock(const LockCallback& callback) {
  callback.Run(static_cast<filesystem::FileError>(lock_table_->LockFile(this)));
}

void FileImpl::Unlock(const UnlockCallback& callback) {
  callback.Run(
      static_cast<filesystem::FileError>(lock_table_->UnlockFile(this)));
}

void FileImpl::AsHandle(const AsHandleCallback& callback) {
  if (!file_.IsValid()) {
    callback.Run(GetError(file_), ScopedHandle());
    return;
  }

  base::File new_file = file_.Duplicate();
  if (!new_file.IsValid()) {
    callback.Run(GetError(new_file), ScopedHandle());
    return;
  }

  base::File::Info info;
  if (!new_file.GetInfo(&info)) {
    callback.Run(FileError::FAILED, ScopedHandle());
    return;
  }

  // Perform one additional check right before we send the file's file
  // descriptor over mojo. This is theoretically redundant, but given that
  // passing a file descriptor to a directory is a sandbox escape on Windows,
  // we should be absolutely paranoid.
  if (info.is_directory) {
    callback.Run(FileError::NOT_A_FILE, ScopedHandle());
    return;
  }

  MojoHandle mojo_handle;
  MojoResult create_result = MojoCreatePlatformHandleWrapper(
      new_file.TakePlatformFile(), &mojo_handle);
  if (create_result != MOJO_RESULT_OK) {
    callback.Run(FileError::FAILED, ScopedHandle());
    return;
  }

  callback.Run(FileError::OK, ScopedHandle(mojo::Handle(mojo_handle)));
}

}  // namespace filesystem
