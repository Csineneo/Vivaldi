// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/v8_inspector/V8HeapProfilerAgentImpl.h"

#include "platform/v8_inspector/InjectedScript.h"
#include "platform/v8_inspector/V8DebuggerImpl.h"
#include "platform/v8_inspector/V8InspectorSessionImpl.h"
#include "platform/v8_inspector/V8StringUtil.h"
#include "platform/v8_inspector/public/V8DebuggerClient.h"
#include <v8-profiler.h>
#include <v8-version.h>

namespace blink {

namespace HeapProfilerAgentState {
static const char heapProfilerEnabled[] = "heapProfilerEnabled";
static const char heapObjectsTrackingEnabled[] = "heapObjectsTrackingEnabled";
static const char allocationTrackingEnabled[] = "allocationTrackingEnabled";
#if V8_MAJOR_VERSION >= 5
static const char samplingHeapProfilerEnabled[] = "samplingHeapProfilerEnabled";
#endif
}

namespace {

class HeapSnapshotProgress final : public v8::ActivityControl {
public:
    HeapSnapshotProgress(protocol::Frontend::HeapProfiler* frontend)
        : m_frontend(frontend) { }
    ControlOption ReportProgressValue(int done, int total) override
    {
        m_frontend->reportHeapSnapshotProgress(done, total, protocol::Maybe<bool>());
        if (done >= total) {
            m_frontend->reportHeapSnapshotProgress(total, total, true);
        }
        m_frontend->flush();
        return kContinue;
    }
private:
    protocol::Frontend::HeapProfiler* m_frontend;
};

class GlobalObjectNameResolver final : public v8::HeapProfiler::ObjectNameResolver {
public:
    explicit GlobalObjectNameResolver(V8InspectorSessionImpl* session) : m_offset(0), m_session(session)
    {
        m_strings.resize(10000);
    }

    const char* GetName(v8::Local<v8::Object> object) override
    {
        int contextId = V8Debugger::contextId(object->CreationContext());
        if (!contextId)
            return "";
        ErrorString errorString;
        InjectedScript* injectedScript = m_session->findInjectedScript(&errorString, contextId);
        if (!injectedScript)
            return "";
        String16 name = injectedScript->context()->origin();
        size_t length = name.length();
        if (m_offset + length + 1 >= m_strings.size())
            return "";
        for (size_t i = 0; i < length; ++i) {
            UChar ch = name[i];
            m_strings[m_offset + i] = ch > 0xff ? '?' : static_cast<char>(ch);
        }
        m_strings[m_offset + length] = '\0';
        char* result = &*m_strings.begin() + m_offset;
        m_offset += length + 1;
        return result;
    }

private:
    size_t m_offset;
    protocol::Vector<char> m_strings;
    V8InspectorSessionImpl* m_session;
};

class HeapSnapshotOutputStream final : public v8::OutputStream {
public:
    HeapSnapshotOutputStream(protocol::Frontend::HeapProfiler* frontend)
        : m_frontend(frontend) { }
    void EndOfStream() override { }
    int GetChunkSize() override { return 102400; }
    WriteResult WriteAsciiChunk(char* data, int size) override
    {
        m_frontend->addHeapSnapshotChunk(String16(data, size));
        m_frontend->flush();
        return kContinue;
    }
private:
    protocol::Frontend::HeapProfiler* m_frontend;
};

v8::Local<v8::Object> objectByHeapObjectId(v8::Isolate* isolate, int id)
{
    v8::HeapProfiler* profiler = isolate->GetHeapProfiler();
    v8::Local<v8::Value> value = profiler->FindObjectById(id);
    if (value.IsEmpty() || !value->IsObject())
        return v8::Local<v8::Object>();
    return value.As<v8::Object>();
}

class InspectableHeapObject final : public V8RuntimeAgent::Inspectable {
public:
    explicit InspectableHeapObject(int heapObjectId) : m_heapObjectId(heapObjectId) { }
    v8::Local<v8::Value> get(v8::Local<v8::Context> context) override
    {
        return objectByHeapObjectId(context->GetIsolate(), m_heapObjectId);
    }
private:
    int m_heapObjectId;
};

class HeapStatsStream final : public v8::OutputStream {
public:
    HeapStatsStream(protocol::Frontend::HeapProfiler* frontend)
        : m_frontend(frontend)
    {
    }

    void EndOfStream() override { }

    WriteResult WriteAsciiChunk(char* data, int size) override
    {
        ASSERT(false);
        return kAbort;
    }

    WriteResult WriteHeapStatsChunk(v8::HeapStatsUpdate* updateData, int count) override
    {
        ASSERT(count > 0);
        OwnPtr<protocol::Array<int>> statsDiff = protocol::Array<int>::create();
        for (int i = 0; i < count; ++i) {
            statsDiff->addItem(updateData[i].index);
            statsDiff->addItem(updateData[i].count);
            statsDiff->addItem(updateData[i].size);
        }
        m_frontend->heapStatsUpdate(statsDiff.release());
        return kContinue;
    }

private:
    protocol::Frontend::HeapProfiler* m_frontend;
};

} // namespace

V8HeapProfilerAgentImpl::V8HeapProfilerAgentImpl(V8InspectorSessionImpl* session)
    : m_session(session)
    , m_isolate(session->debugger()->isolate())
{
}

V8HeapProfilerAgentImpl::~V8HeapProfilerAgentImpl()
{
}

void V8HeapProfilerAgentImpl::clearFrontend()
{
    ErrorString error;
    disable(&error);
    ASSERT(m_frontend);
    m_frontend = nullptr;
}

void V8HeapProfilerAgentImpl::restore()
{
    if (m_state->booleanProperty(HeapProfilerAgentState::heapProfilerEnabled, false))
        m_frontend->resetProfiles();
    if (m_state->booleanProperty(HeapProfilerAgentState::heapObjectsTrackingEnabled, false))
        startTrackingHeapObjectsInternal(m_state->booleanProperty(HeapProfilerAgentState::allocationTrackingEnabled, false));
#if V8_MAJOR_VERSION >= 5
    if (m_state->booleanProperty(HeapProfilerAgentState::samplingHeapProfilerEnabled, false)) {
        ErrorString error;
        startSampling(&error);
    }
#endif
}

void V8HeapProfilerAgentImpl::collectGarbage(ErrorString*)
{
    m_isolate->LowMemoryNotification();
}

void V8HeapProfilerAgentImpl::startTrackingHeapObjects(ErrorString*, const protocol::Maybe<bool>& trackAllocations)
{
    m_state->setBoolean(HeapProfilerAgentState::heapObjectsTrackingEnabled, true);
    bool allocationTrackingEnabled = trackAllocations.fromMaybe(false);
    m_state->setBoolean(HeapProfilerAgentState::allocationTrackingEnabled, allocationTrackingEnabled);
    startTrackingHeapObjectsInternal(allocationTrackingEnabled);
}

void V8HeapProfilerAgentImpl::stopTrackingHeapObjects(ErrorString* error, const protocol::Maybe<bool>& reportProgress)
{
    requestHeapStatsUpdate();
    takeHeapSnapshot(error, reportProgress);
    stopTrackingHeapObjectsInternal();
}

void V8HeapProfilerAgentImpl::enable(ErrorString*)
{
    m_state->setBoolean(HeapProfilerAgentState::heapProfilerEnabled, true);
}

void V8HeapProfilerAgentImpl::disable(ErrorString* error)
{
    stopTrackingHeapObjectsInternal();
#if V8_MAJOR_VERSION >= 5
    if (m_state->booleanProperty(HeapProfilerAgentState::samplingHeapProfilerEnabled, false)) {
        v8::HeapProfiler* profiler = m_isolate->GetHeapProfiler();
        if (profiler)
            profiler->StopSamplingHeapProfiler();
    }
#endif
    m_isolate->GetHeapProfiler()->ClearObjectIds();
    m_state->setBoolean(HeapProfilerAgentState::heapProfilerEnabled, false);
}

void V8HeapProfilerAgentImpl::takeHeapSnapshot(ErrorString* errorString, const protocol::Maybe<bool>& reportProgress)
{
    v8::HeapProfiler* profiler = m_isolate->GetHeapProfiler();
    if (!profiler) {
        *errorString = "Cannot access v8 heap profiler";
        return;
    }
    OwnPtr<HeapSnapshotProgress> progress;
    if (reportProgress.fromMaybe(false))
        progress = adoptPtr(new HeapSnapshotProgress(m_frontend));

    GlobalObjectNameResolver resolver(m_session);
    const v8::HeapSnapshot* snapshot = profiler->TakeHeapSnapshot(progress.get(), &resolver);
    if (!snapshot) {
        *errorString = "Failed to take heap snapshot";
        return;
    }
    HeapSnapshotOutputStream stream(m_frontend);
    snapshot->Serialize(&stream);
    const_cast<v8::HeapSnapshot*>(snapshot)->Delete();
}

void V8HeapProfilerAgentImpl::getObjectByHeapObjectId(ErrorString* error, const String16& heapSnapshotObjectId, const protocol::Maybe<String16>& objectGroup, OwnPtr<protocol::Runtime::RemoteObject>* result)
{
    bool ok;
    int id = heapSnapshotObjectId.toInt(&ok);
    if (!ok) {
        *error = "Invalid heap snapshot object id";
        return;
    }

    v8::HandleScope handles(m_isolate);
    v8::Local<v8::Object> heapObject = objectByHeapObjectId(m_isolate, id);
    if (heapObject.IsEmpty()) {
        *error = "Object is not available";
        return;
    }
    *result = m_session->runtimeAgent()->wrapObject(heapObject->CreationContext(), heapObject, objectGroup.fromMaybe(""));
    if (!result)
        *error = "Object is not available";
}

void V8HeapProfilerAgentImpl::addInspectedHeapObject(ErrorString* errorString, const String16& inspectedHeapObjectId)
{
    bool ok;
    int id = inspectedHeapObjectId.toInt(&ok);
    if (!ok) {
        *errorString = "Invalid heap snapshot object id";
        return;
    }
    m_session->runtimeAgent()->addInspectedObject(adoptPtr(new InspectableHeapObject(id)));
}

void V8HeapProfilerAgentImpl::getHeapObjectId(ErrorString* errorString, const String16& objectId, String16* heapSnapshotObjectId)
{
    v8::HandleScope handles(m_isolate);
    v8::Local<v8::Value> value = m_session->runtimeAgent()->findObject(errorString, objectId);
    if (value.IsEmpty() || value->IsUndefined())
        return;

    v8::SnapshotObjectId id = m_isolate->GetHeapProfiler()->GetObjectId(value);
    *heapSnapshotObjectId = String16::number(id);
}

void V8HeapProfilerAgentImpl::requestHeapStatsUpdate()
{
    if (!m_frontend)
        return;
    HeapStatsStream stream(m_frontend);
    v8::SnapshotObjectId lastSeenObjectId = m_isolate->GetHeapProfiler()->GetHeapStats(&stream);
    m_frontend->lastSeenObjectId(lastSeenObjectId, m_session->debugger()->client()->currentTimeMS());
}

void V8HeapProfilerAgentImpl::startTrackingHeapObjectsInternal(bool trackAllocations)
{
    m_isolate->GetHeapProfiler()->StartTrackingHeapObjects(trackAllocations);
}

void V8HeapProfilerAgentImpl::stopTrackingHeapObjectsInternal()
{
    m_isolate->GetHeapProfiler()->StopTrackingHeapObjects();
    m_state->setBoolean(HeapProfilerAgentState::heapObjectsTrackingEnabled, false);
    m_state->setBoolean(HeapProfilerAgentState::allocationTrackingEnabled, false);
}

void V8HeapProfilerAgentImpl::startSampling(ErrorString* errorString)
{
#if V8_MAJOR_VERSION >= 5
    v8::HeapProfiler* profiler = m_isolate->GetHeapProfiler();
    if (!profiler) {
        *errorString = "Cannot access v8 heap profiler";
        return;
    }
    m_state->setBoolean(HeapProfilerAgentState::samplingHeapProfilerEnabled, true);
    profiler->StartSamplingHeapProfiler();
#endif
}

#if V8_MAJOR_VERSION >= 5
namespace {
PassOwnPtr<protocol::HeapProfiler::SamplingHeapProfileNode> buildSampingHeapProfileNode(const v8::AllocationProfile::Node* node)
{
    auto children = protocol::Array<protocol::HeapProfiler::SamplingHeapProfileNode>::create();
    for (const auto* child : node->children)
        children->addItem(buildSampingHeapProfileNode(child));
    size_t totalSize = 0;
    for (const auto& allocation : node->allocations)
        totalSize += allocation.size * allocation.count;
    OwnPtr<protocol::HeapProfiler::SamplingHeapProfileNode> result = protocol::HeapProfiler::SamplingHeapProfileNode::create()
        .setFunctionName(toProtocolString(node->name))
        .setScriptId(String16::number(node->script_id))
        .setUrl(toProtocolString(node->script_name))
        .setLineNumber(node->line_number)
        .setColumnNumber(node->column_number)
        .setTotalSize(totalSize)
        .setChildren(children).build();
    return result.release();
}
} // namespace
#endif

void V8HeapProfilerAgentImpl::stopSampling(ErrorString* errorString, OwnPtr<protocol::HeapProfiler::SamplingHeapProfile>* profile)
{
#if V8_MAJOR_VERSION >= 5
    v8::HeapProfiler* profiler = m_isolate->GetHeapProfiler();
    if (!profiler) {
        *errorString = "Cannot access v8 heap profiler";
        return;
    }
    v8::HandleScope scope(m_isolate); // Allocation profile contains Local handles.
    OwnPtr<v8::AllocationProfile> v8Profile = adoptPtr(profiler->GetAllocationProfile());
    profiler->StopSamplingHeapProfiler();
    m_state->setBoolean(HeapProfilerAgentState::samplingHeapProfilerEnabled, false);
    if (!v8Profile) {
        *errorString = "Cannot access v8 sampled heap profile.";
        return;
    }
    v8::AllocationProfile::Node* root = v8Profile->GetRootNode();
    *profile = protocol::HeapProfiler::SamplingHeapProfile::create()
        .setHead(buildSampingHeapProfileNode(root)).build();
#endif
}

} // namespace blink
