// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/heap/BlinkGCMemoryDumpProvider.h"

#include "base/trace_event/heap_profiler_allocation_context_tracker.h"
#include "base/trace_event/heap_profiler_allocation_register.h"
#include "base/trace_event/trace_event_memory_overhead.h"
#include "platform/heap/Handle.h"
#include "platform/web_process_memory_dump_impl.h"
#include "public/platform/Platform.h"
#include "public/platform/WebMemoryAllocatorDump.h"
#include "wtf/StdLibExtras.h"
#include "wtf/Threading.h"

namespace blink {
namespace {

void dumpMemoryTotals(blink::WebProcessMemoryDump* memoryDump)
{
    String dumpName = String::format("blink_gc");
    WebMemoryAllocatorDump* allocatorDump = memoryDump->createMemoryAllocatorDump(dumpName);
    allocatorDump->addScalar("size", "bytes", Heap::allocatedSpace());

    dumpName.append("/allocated_objects");
    WebMemoryAllocatorDump* objectsDump = memoryDump->createMemoryAllocatorDump(dumpName);

    // Heap::markedObjectSize() can be underestimated if we're still in the
    // process of lazy sweeping.
    objectsDump->addScalar("size", "bytes", Heap::allocatedObjectSize() + Heap::markedObjectSize());
}

void reportAllocation(Address address, size_t size, const char* typeName)
{
    BlinkGCMemoryDumpProvider::instance()->insert(address, size, typeName);
}

void reportFree(Address address)
{
    BlinkGCMemoryDumpProvider::instance()->remove(address);
}

} // namespace

BlinkGCMemoryDumpProvider* BlinkGCMemoryDumpProvider::instance()
{
    DEFINE_STATIC_LOCAL(BlinkGCMemoryDumpProvider, instance, ());
    return &instance;
}

BlinkGCMemoryDumpProvider::~BlinkGCMemoryDumpProvider()
{
}

bool BlinkGCMemoryDumpProvider::onMemoryDump(WebMemoryDumpLevelOfDetail levelOfDetail, blink::WebProcessMemoryDump* memoryDump)
{
    if (levelOfDetail == WebMemoryDumpLevelOfDetail::Light) {
        dumpMemoryTotals(memoryDump);
        return true;
    }

    Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::TakeSnapshot, BlinkGC::ForcedGC);
    dumpMemoryTotals(memoryDump);

    if (m_isHeapProfilingEnabled) {
        base::trace_event::TraceEventMemoryOverhead overhead;
        base::hash_map<base::trace_event::AllocationContext, size_t> bytesByContext;
        {
            MutexLocker locker(m_allocationRegisterMutex);
            for (const auto& allocSize : *m_allocationRegister)
                bytesByContext[allocSize.context] += allocSize.size;

            m_allocationRegister->EstimateTraceMemoryOverhead(&overhead);
        }
        memoryDump->dumpHeapUsage(bytesByContext, overhead, "blink_gc");
    }

    // Merge all dumps collected by Heap::collectGarbage.
    memoryDump->takeAllDumpsFrom(m_currentProcessMemoryDump.get());
    return true;
}

void BlinkGCMemoryDumpProvider::onHeapProfilingEnabled(bool enabled)
{
    if (enabled) {
        {
            MutexLocker locker(m_allocationRegisterMutex);
            if (!m_allocationRegister)
                m_allocationRegister = adoptPtr(new base::trace_event::AllocationRegister());
        }
        HeapAllocHooks::setAllocationHook(reportAllocation);
        HeapAllocHooks::setFreeHook(reportFree);
    } else {
        HeapAllocHooks::setAllocationHook(nullptr);
        HeapAllocHooks::setFreeHook(nullptr);
    }
    m_isHeapProfilingEnabled = enabled;
}

WebMemoryAllocatorDump* BlinkGCMemoryDumpProvider::createMemoryAllocatorDumpForCurrentGC(const String& absoluteName)
{
    return m_currentProcessMemoryDump->createMemoryAllocatorDump(absoluteName);
}

void BlinkGCMemoryDumpProvider::clearProcessDumpForCurrentGC()
{
    m_currentProcessMemoryDump->clear();
}

BlinkGCMemoryDumpProvider::BlinkGCMemoryDumpProvider()
    : m_currentProcessMemoryDump(adoptPtr(new WebProcessMemoryDumpImpl()))
    , m_isHeapProfilingEnabled(false)
{
}

void BlinkGCMemoryDumpProvider::insert(Address address, size_t size, const char* typeName)
{
    base::trace_event::AllocationContext context = base::trace_event::AllocationContextTracker::GetContextSnapshot();
    context.type_name = typeName;
    MutexLocker locker(m_allocationRegisterMutex);
    if (m_allocationRegister)
        m_allocationRegister->Insert(address, size, context);
}

void BlinkGCMemoryDumpProvider::remove(Address address)
{
    MutexLocker locker(m_allocationRegisterMutex);
    if (m_allocationRegister)
        m_allocationRegister->Remove(address);
}

} // namespace blink
