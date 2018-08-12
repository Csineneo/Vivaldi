/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MainThreadTaskRunner_h
#define MainThreadTaskRunner_h

#include "core/CoreExport.h"
#include "platform/Timer.h"
#include "platform/heap/Handle.h"
#include "wtf/Allocator.h"
#include "wtf/Noncopyable.h"
#include "wtf/Vector.h"
#include "wtf/WeakPtr.h"

namespace blink {

class ExecutionContext;
class ExecutionContextTask;

class CORE_EXPORT MainThreadTaskRunner final {
    USING_FAST_MALLOC(MainThreadTaskRunner);
    WTF_MAKE_NONCOPYABLE(MainThreadTaskRunner);
public:
    static PassOwnPtr<MainThreadTaskRunner> create(ExecutionContext*);

    ~MainThreadTaskRunner();

    DECLARE_TRACE();

    void postTask(const WebTraceLocation&, std::unique_ptr<ExecutionContextTask>); // Executes the task on context's thread asynchronously.
    void postInspectorTask(const WebTraceLocation&, std::unique_ptr<ExecutionContextTask>);
    void perform(std::unique_ptr<ExecutionContextTask>, bool);

    void suspend();
    void resume();

private:
    explicit MainThreadTaskRunner(ExecutionContext*);

    void pendingTasksTimerFired(Timer<MainThreadTaskRunner>*);

    void postTaskInternal(const WebTraceLocation&, std::unique_ptr<ExecutionContextTask>, bool isInspectorTask);

    // Untraced back reference to the owner Document;
    // this object has identical lifetime to it.
    UntracedMember<ExecutionContext> m_context;
    Timer<MainThreadTaskRunner> m_pendingTasksTimer;
    Vector<std::unique_ptr<ExecutionContextTask>> m_pendingTasks;
    bool m_suspended;
    WeakPtrFactory<MainThreadTaskRunner> m_weakFactory;
};

inline PassOwnPtr<MainThreadTaskRunner> MainThreadTaskRunner::create(ExecutionContext* context)
{
    return adoptPtr(new MainThreadTaskRunner(context));
}

} // namespace blink

#endif
