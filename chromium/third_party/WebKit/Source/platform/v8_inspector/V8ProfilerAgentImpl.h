// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8ProfilerAgentImpl_h
#define V8ProfilerAgentImpl_h

#include "platform/inspector_protocol/Allocator.h"
#include "platform/inspector_protocol/Frontend.h"
#include "platform/inspector_protocol/String16.h"
#include "platform/v8_inspector/public/V8ProfilerAgent.h"

namespace v8 {
class Isolate;
}

namespace blink {

class V8InspectorSessionImpl;

class V8ProfilerAgentImpl : public V8ProfilerAgent {
    PROTOCOL_DISALLOW_COPY(V8ProfilerAgentImpl);
public:
    explicit V8ProfilerAgentImpl(V8InspectorSessionImpl*);
    ~V8ProfilerAgentImpl() override;

    bool enabled() const { return m_enabled; }

    void setInspectorState(protocol::DictionaryValue* state) override { m_state = state; }
    void setFrontend(protocol::Frontend::Profiler* frontend) override { m_frontend = frontend; }
    void clearFrontend() override;
    void restore() override;

    void enable(ErrorString*) override;
    void disable(ErrorString*) override;
    void setSamplingInterval(ErrorString*, int) override;
    void start(ErrorString*) override;
    void stop(ErrorString*, OwnPtr<protocol::Profiler::CPUProfile>*) override;

    void consoleProfile(const String16& title);
    void consoleProfileEnd(const String16& title);

private:
    String16 nextProfileId();

    void startProfiling(const String16& title);
    PassOwnPtr<protocol::Profiler::CPUProfile> stopProfiling(const String16& title, bool serialize);

    bool isRecording() const;

    V8InspectorSessionImpl* m_session;
    v8::Isolate* m_isolate;
    protocol::DictionaryValue* m_state;
    protocol::Frontend::Profiler* m_frontend;
    bool m_enabled;
    bool m_recordingCPUProfile;
    class ProfileDescriptor;
    protocol::Vector<ProfileDescriptor> m_startedProfiles;
    String16 m_frontendInitiatedProfileId;
};

} // namespace blink


#endif // !defined(V8ProfilerAgentImpl_h)
