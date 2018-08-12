/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#include "core/inspector/InspectorDebuggerAgent.h"

#include "bindings/core/v8/V8Binding.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/v8_inspector/public/V8DebuggerAgent.h"

namespace blink {

using protocol::Maybe;

namespace DebuggerAgentState {
static const char debuggerEnabled[] = "debuggerEnabled";
}

InspectorDebuggerAgent::InspectorDebuggerAgent(V8DebuggerAgent* agent)
    : InspectorBaseAgent<InspectorDebuggerAgent, protocol::Frontend::Debugger>("Debugger")
    , m_v8DebuggerAgent(agent)
{
}

InspectorDebuggerAgent::~InspectorDebuggerAgent()
{
#if !ENABLE(OILPAN)
    ASSERT(!m_instrumentingAgents->inspectorDebuggerAgent());
#endif
}

DEFINE_TRACE(InspectorDebuggerAgent)
{
    InspectorBaseAgent<InspectorDebuggerAgent, protocol::Frontend::Debugger>::trace(visitor);
}

// Protocol implementation.
void InspectorDebuggerAgent::enable(ErrorString* errorString)
{
    m_v8DebuggerAgent->enable(errorString);
    m_instrumentingAgents->setInspectorDebuggerAgent(this);
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, true);
}

void InspectorDebuggerAgent::disable(ErrorString* errorString)
{
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, false);
    m_instrumentingAgents->setInspectorDebuggerAgent(nullptr);
    m_v8DebuggerAgent->disable(errorString);
}

void InspectorDebuggerAgent::setBreakpointsActive(ErrorString* errorString, bool inActive)
{
    m_v8DebuggerAgent->setBreakpointsActive(errorString, inActive);
}

void InspectorDebuggerAgent::setSkipAllPauses(ErrorString* errorString, bool inSkipped)
{
    m_v8DebuggerAgent->setSkipAllPauses(errorString, inSkipped);
}

void InspectorDebuggerAgent::setBreakpointByUrl(ErrorString* errorString,
    int inLineNumber,
    const Maybe<String16>& inUrl,
    const Maybe<String16>& inUrlRegex,
    const Maybe<int>& inColumnNumber,
    const Maybe<String16>& inCondition,
    protocol::Debugger::BreakpointId* outBreakpointId,
    OwnPtr<Array<protocol::Debugger::Location>>* outLocations)
{
    m_v8DebuggerAgent->setBreakpointByUrl(errorString, inLineNumber, inUrl, inUrlRegex, inColumnNumber, inCondition, outBreakpointId, outLocations);
}

void InspectorDebuggerAgent::setBreakpoint(ErrorString* errorString, PassOwnPtr<protocol::Debugger::Location> inLocation,
    const Maybe<String16>& inCondition,
    protocol::Debugger::BreakpointId* outBreakpointId,
    OwnPtr<protocol::Debugger::Location>* outActualLocation)
{
    m_v8DebuggerAgent->setBreakpoint(errorString, inLocation, inCondition, outBreakpointId, outActualLocation);
}

void InspectorDebuggerAgent::removeBreakpoint(ErrorString* errorString,
    const String16& inBreakpointId)
{
    m_v8DebuggerAgent->removeBreakpoint(errorString, inBreakpointId);
}

void InspectorDebuggerAgent::continueToLocation(ErrorString* errorString,
    PassOwnPtr<protocol::Debugger::Location> inLocation,
    const Maybe<bool>& inInterstatementLocation)
{
    m_v8DebuggerAgent->continueToLocation(errorString, inLocation, inInterstatementLocation);
}

void InspectorDebuggerAgent::stepOver(ErrorString* errorString)
{
    m_v8DebuggerAgent->stepOver(errorString);
}

void InspectorDebuggerAgent::stepInto(ErrorString* errorString)
{
    m_v8DebuggerAgent->stepInto(errorString);
}

void InspectorDebuggerAgent::stepOut(ErrorString* errorString)
{
    m_v8DebuggerAgent->stepOut(errorString);
}

void InspectorDebuggerAgent::pause(ErrorString* errorString)
{
    m_v8DebuggerAgent->pause(errorString);
}

void InspectorDebuggerAgent::resume(ErrorString* errorString)
{
    m_v8DebuggerAgent->resume(errorString);
}

void InspectorDebuggerAgent::searchInContent(ErrorString* errorString,
    const String16& inScriptId,
    const String16& inQuery,
    const Maybe<bool>& inCaseSensitive,
    const Maybe<bool>& inIsRegex,
    OwnPtr<Array<protocol::Debugger::SearchMatch>>* outResult)
{
    m_v8DebuggerAgent->searchInContent(errorString, inScriptId, inQuery, inCaseSensitive, inIsRegex, outResult);
}

void InspectorDebuggerAgent::canSetScriptSource(ErrorString* errorString, bool* outResult)
{
    m_v8DebuggerAgent->canSetScriptSource(errorString, outResult);
}

void InspectorDebuggerAgent::setScriptSource(ErrorString* errorString,
    const String16& inScriptId,
    const String16& inScriptSource,
    const Maybe<bool>& inPreview,
    Maybe<Array<protocol::Debugger::CallFrame>>* optOutCallFrames,
    Maybe<bool>* optOutStackChanged,
    Maybe<protocol::Runtime::StackTrace>* optOutAsyncStackTrace,
    Maybe<protocol::Debugger::SetScriptSourceError>* optOutCompileError)
{
    m_v8DebuggerAgent->setScriptSource(errorString, inScriptId, inScriptSource, inPreview, optOutCallFrames, optOutStackChanged, optOutAsyncStackTrace, optOutCompileError);
}

void InspectorDebuggerAgent::restartFrame(ErrorString* errorString,
    const String16& inCallFrameId,
    OwnPtr<Array<protocol::Debugger::CallFrame>>* outCallFrames,
    Maybe<protocol::Runtime::StackTrace>* optOutAsyncStackTrace)
{
    m_v8DebuggerAgent->restartFrame(errorString, inCallFrameId, outCallFrames, optOutAsyncStackTrace);
}

void InspectorDebuggerAgent::getScriptSource(ErrorString* errorString,
    const String16& inScriptId,
    String16* outScriptSource)
{
    m_v8DebuggerAgent->getScriptSource(errorString, inScriptId, outScriptSource);
}

void InspectorDebuggerAgent::getFunctionDetails(ErrorString* errorString,
    const String16& inFunctionId,
    OwnPtr<protocol::Debugger::FunctionDetails>* outDetails)
{
    m_v8DebuggerAgent->getFunctionDetails(errorString, inFunctionId, outDetails);
}

void InspectorDebuggerAgent::getGeneratorObjectDetails(ErrorString* errorString,
    const String16& inObjectId,
    OwnPtr<protocol::Debugger::GeneratorObjectDetails>* outDetails)
{
    m_v8DebuggerAgent->getGeneratorObjectDetails(errorString, inObjectId, outDetails);
}

void InspectorDebuggerAgent::getCollectionEntries(ErrorString* errorString,
    const String16& inObjectId,
    OwnPtr<Array<protocol::Debugger::CollectionEntry>>* outEntries)
{
    m_v8DebuggerAgent->getCollectionEntries(errorString, inObjectId, outEntries);
}

void InspectorDebuggerAgent::setPauseOnExceptions(ErrorString* errorString,
    const String16& inState)
{
    m_v8DebuggerAgent->setPauseOnExceptions(errorString, inState);
}

void InspectorDebuggerAgent::evaluateOnCallFrame(ErrorString* errorString,
    const String16& inCallFrameId,
    const String16& inExpression,
    const Maybe<String16>& inObjectGroup,
    const Maybe<bool>& inIncludeCommandLineAPI,
    const Maybe<bool>& inDoNotPauseOnExceptionsAndMuteConsole,
    const Maybe<bool>& inReturnByValue,
    const Maybe<bool>& inGeneratePreview,
    OwnPtr<protocol::Runtime::RemoteObject>* outResult,
    Maybe<bool>* optOutWasThrown,
    Maybe<protocol::Runtime::ExceptionDetails>* optOutExceptionDetails)
{
    m_v8DebuggerAgent->evaluateOnCallFrame(errorString, inCallFrameId, inExpression, inObjectGroup, inIncludeCommandLineAPI, inDoNotPauseOnExceptionsAndMuteConsole, inReturnByValue, inGeneratePreview, outResult, optOutWasThrown, optOutExceptionDetails);
}

void InspectorDebuggerAgent::setVariableValue(ErrorString* errorString, int inScopeNumber,
    const String16& inVariableName,
    PassOwnPtr<protocol::Runtime::CallArgument> inNewValue,
    const String16& inCallFrameId)
{
    m_v8DebuggerAgent->setVariableValue(errorString, inScopeNumber, inVariableName, inNewValue, inCallFrameId);
}

void InspectorDebuggerAgent::getBacktrace(ErrorString* errorString,
    OwnPtr<Array<protocol::Debugger::CallFrame>>* outCallFrames,
    Maybe<protocol::Runtime::StackTrace>* optOutAsyncStackTrace)
{
    m_v8DebuggerAgent->getBacktrace(errorString, outCallFrames, optOutAsyncStackTrace);
}

void InspectorDebuggerAgent::setAsyncCallStackDepth(ErrorString* errorString, int inMaxDepth)
{
    m_v8DebuggerAgent->setAsyncCallStackDepth(errorString, inMaxDepth);
}

void InspectorDebuggerAgent::setBlackboxedRanges(
    ErrorString* errorString,
    const String16& inScriptId,
    PassOwnPtr<protocol::Array<protocol::Debugger::ScriptPosition>> inPositions)
{
    m_v8DebuggerAgent->setBlackboxedRanges(errorString, inScriptId, inPositions);
}

bool InspectorDebuggerAgent::isPaused()
{
    return m_v8DebuggerAgent->isPaused();
}

void InspectorDebuggerAgent::scriptExecutionBlockedByCSP(const String& directiveText)
{
    OwnPtr<protocol::DictionaryValue> directive = protocol::DictionaryValue::create();
    directive->setString("directiveText", directiveText);
    m_v8DebuggerAgent->breakProgramOnException(protocol::Debugger::Paused::ReasonEnum::CSPViolation, directive.release());
}

void InspectorDebuggerAgent::willExecuteScript(int scriptId)
{
    m_v8DebuggerAgent->willExecuteScript(scriptId);
}

void InspectorDebuggerAgent::didExecuteScript()
{
    m_v8DebuggerAgent->didExecuteScript();
}

void InspectorDebuggerAgent::asyncTaskScheduled(const String& taskName, void* task)
{
    m_v8DebuggerAgent->asyncTaskScheduled(taskName, task, false);
}

void InspectorDebuggerAgent::asyncTaskScheduled(const String& operationName, void* task, bool recurring)
{
    m_v8DebuggerAgent->asyncTaskScheduled(operationName, task, recurring);
}

void InspectorDebuggerAgent::asyncTaskCanceled(void* task)
{
    m_v8DebuggerAgent->asyncTaskCanceled(task);
}

void InspectorDebuggerAgent::allAsyncTasksCanceled()
{
    m_v8DebuggerAgent->allAsyncTasksCanceled();
}

void InspectorDebuggerAgent::asyncTaskStarted(void* task)
{
    m_v8DebuggerAgent->asyncTaskStarted(task);
}

void InspectorDebuggerAgent::asyncTaskFinished(void* task)
{
    m_v8DebuggerAgent->asyncTaskFinished(task);
}

// InspectorBaseAgent overrides.
void InspectorDebuggerAgent::setState(protocol::DictionaryValue* state)
{
    InspectorBaseAgent::setState(state);
    m_v8DebuggerAgent->setInspectorState(m_state);
}

void InspectorDebuggerAgent::setFrontend(protocol::Frontend* frontend)
{
    InspectorBaseAgent::setFrontend(frontend);
    m_v8DebuggerAgent->setFrontend(protocol::Frontend::Debugger::from(frontend));
}

void InspectorDebuggerAgent::clearFrontend()
{
    m_v8DebuggerAgent->clearFrontend();
    InspectorBaseAgent::clearFrontend();
}

void InspectorDebuggerAgent::restore()
{
    if (!m_state->booleanProperty(DebuggerAgentState::debuggerEnabled, false))
        return;
    m_v8DebuggerAgent->restore();
    ErrorString errorString;
    enable(&errorString);
}

} // namespace blink
