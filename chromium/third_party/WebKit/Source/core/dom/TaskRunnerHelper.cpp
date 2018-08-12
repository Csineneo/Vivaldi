// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/TaskRunnerHelper.h"

#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/LocalFrame.h"
#include "platform/WebFrameScheduler.h"
#include "platform/WebTaskRunner.h"
#include "public/platform/Platform.h"
#include "public/platform/WebThread.h"

namespace blink {

RefPtr<WebTaskRunner> TaskRunnerHelper::Get(TaskType type, LocalFrame* frame) {
  // TODO(haraken): Optimize the mapping from TaskTypes to task runners.
  switch (type) {
    case TaskType::kTimer:
      return frame ? frame->FrameScheduler()->TimerTaskRunner()
                   : Platform::Current()->CurrentThread()->GetWebTaskRunner();
    case TaskType::kUnspecedLoading:
    case TaskType::kNetworking:
      return frame ? frame->FrameScheduler()->LoadingTaskRunner()
                   : Platform::Current()->CurrentThread()->GetWebTaskRunner();
    // Throttling following tasks may break existing web pages, so tentatively
    // these are unthrottled.
    // TODO(nhiroki): Throttle them again after we're convinced that it's safe
    // or provide a mechanism that web pages can opt-out it if throttling is not
    // desirable.
    case TaskType::kDatabaseAccess:
      return frame ? frame->FrameScheduler()->SuspendableTaskRunner()
                   : Platform::Current()->CurrentThread()->GetWebTaskRunner();
    case TaskType::kDOMManipulation:
    case TaskType::kUserInteraction:
    case TaskType::kHistoryTraversal:
    case TaskType::kEmbed:
    case TaskType::kMediaElementEvent:
    case TaskType::kCanvasBlobSerialization:
    case TaskType::kRemoteEvent:
    case TaskType::kWebSocket:
    case TaskType::kMicrotask:
    case TaskType::kPostedMessage:
    case TaskType::kUnshippedPortMessage:
    case TaskType::kFileReading:
    case TaskType::kPresentation:
    case TaskType::kSensor:
    case TaskType::kPerformanceTimeline:
    case TaskType::kWebGL:
    case TaskType::kUnspecedTimer:
    case TaskType::kMiscPlatformAPI:
    case TaskType::kUnthrottled:
      return frame ? frame->FrameScheduler()->UnthrottledTaskRunner()
                   : Platform::Current()->CurrentThread()->GetWebTaskRunner();
  }
  NOTREACHED();
  return nullptr;
}

RefPtr<WebTaskRunner> TaskRunnerHelper::Get(TaskType type, Document* document) {
  return Get(type, document ? document->GetFrame() : nullptr);
}

RefPtr<WebTaskRunner> TaskRunnerHelper::Get(
    TaskType type,
    ExecutionContext* execution_context) {
  return Get(type, execution_context && execution_context->IsDocument()
                       ? static_cast<Document*>(execution_context)
                       : nullptr);
}

RefPtr<WebTaskRunner> TaskRunnerHelper::Get(TaskType type,
                                            ScriptState* script_state) {
  return Get(type,
             script_state ? ExecutionContext::From(script_state) : nullptr);
}

}  // namespace blink
