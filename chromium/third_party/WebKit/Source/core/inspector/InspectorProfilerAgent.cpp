/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/inspector/InspectorProfilerAgent.h"

#include "bindings/core/v8/ScriptCallStack.h"
#include "bindings/core/v8/V8Binding.h"
#include "platform/v8_inspector/public/V8ProfilerAgent.h"

namespace blink {

InspectorProfilerAgent::InspectorProfilerAgent(V8ProfilerAgent* agent)
    : InspectorBaseAgent<InspectorProfilerAgent, protocol::Frontend::Profiler>("Profiler")
    , m_v8ProfilerAgent(agent)
{
}

InspectorProfilerAgent::~InspectorProfilerAgent()
{
}

// InspectorBaseAgent overrides.
void InspectorProfilerAgent::init(InstrumentingAgents* instrumentingAgents, protocol::Frontend* baseFrontend, protocol::Dispatcher* dispatcher, protocol::DictionaryValue* state)
{
    InspectorBaseAgent::init(instrumentingAgents, baseFrontend, dispatcher, state);
    m_v8ProfilerAgent->setInspectorState(m_state);
    m_v8ProfilerAgent->setFrontend(frontend());
}

void InspectorProfilerAgent::dispose()
{
    m_v8ProfilerAgent->clearFrontend();
    InspectorBaseAgent::dispose();
}

void InspectorProfilerAgent::restore()
{
    m_v8ProfilerAgent->restore();
}

void InspectorProfilerAgent::enable(ErrorString* errorString)
{
    m_v8ProfilerAgent->enable(errorString);
}

void InspectorProfilerAgent::disable(ErrorString* errorString)
{
    m_v8ProfilerAgent->disable(errorString);
}

void InspectorProfilerAgent::setSamplingInterval(ErrorString* error, int interval)
{
    m_v8ProfilerAgent->setSamplingInterval(error, interval);
}

void InspectorProfilerAgent::start(ErrorString* error)
{
    m_v8ProfilerAgent->start(error);
}

void InspectorProfilerAgent::stop(ErrorString* errorString, OwnPtr<protocol::Profiler::CPUProfile>* profile)
{
    m_v8ProfilerAgent->stop(errorString, profile);
}

DEFINE_TRACE(InspectorProfilerAgent)
{
    InspectorBaseAgent::trace(visitor);
}

} // namespace blink
