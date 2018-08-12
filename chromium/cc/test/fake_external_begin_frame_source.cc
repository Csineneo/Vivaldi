// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_external_begin_frame_source.h"

#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "cc/test/begin_frame_args_test.h"

namespace cc {

FakeExternalBeginFrameSource::FakeExternalBeginFrameSource(double refresh_rate)
    : milliseconds_per_frame_(1000.0 / refresh_rate),
      weak_ptr_factory_(this) {
  DetachFromThread();
}

FakeExternalBeginFrameSource::~FakeExternalBeginFrameSource() {
  DCHECK(CalledOnValidThread());
}

void FakeExternalBeginFrameSource::OnNeedsBeginFramesChanged(
    bool needs_begin_frames) {
  DCHECK(CalledOnValidThread());
  if (needs_begin_frames) {
    PostTestOnBeginFrame();
  } else {
    begin_frame_task_.Cancel();
  }
}

void FakeExternalBeginFrameSource::TestOnBeginFrame() {
  DCHECK(CalledOnValidThread());
  CallOnBeginFrame(CreateBeginFrameArgsForTesting(BEGINFRAME_FROM_HERE));
  PostTestOnBeginFrame();
}

void FakeExternalBeginFrameSource::PostTestOnBeginFrame() {
  begin_frame_task_.Reset(
      base::Bind(&FakeExternalBeginFrameSource::TestOnBeginFrame,
                 weak_ptr_factory_.GetWeakPtr()));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, begin_frame_task_.callback(),
      base::TimeDelta::FromMilliseconds(milliseconds_per_frame_));
}

}  // namespace cc
