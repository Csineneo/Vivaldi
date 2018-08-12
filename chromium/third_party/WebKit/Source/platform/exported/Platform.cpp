/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
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

#include "base/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "platform/PartitionAllocMemoryDumpProvider.h"
#include "platform/graphics/CompositorFactory.h"
#include "platform/web_memory_dump_provider_adapter.h"
#include "public/platform/Platform.h"
#include "wtf/HashMap.h"
#include "wtf/OwnPtr.h"

namespace blink {

static Platform* s_platform = 0;
using ProviderToAdapterMap = HashMap<WebMemoryDumpProvider*, OwnPtr<WebMemoryDumpProviderAdapter>>;

namespace {

ProviderToAdapterMap& memoryDumpProviders()
{
    DEFINE_STATIC_LOCAL(ProviderToAdapterMap, providerToAdapterMap, ());
    return providerToAdapterMap;
}

} // namespace

Platform::Platform()
    : m_mainThread(0)
{
}

void Platform::initialize(Platform* platform)
{
    s_platform = platform;
    if (s_platform)
        s_platform->m_mainThread = platform->currentThread();

    // TODO(ssid): remove this check after fixing crbug.com/486782.
    if (s_platform && s_platform->m_mainThread)
        s_platform->registerMemoryDumpProvider(PartitionAllocMemoryDumpProvider::instance(), "PartitionAlloc");

    CompositorFactory::initializeDefault();
}

void Platform::shutdown()
{
    CompositorFactory::shutdown();

    if (s_platform->m_mainThread)
        s_platform->unregisterMemoryDumpProvider(PartitionAllocMemoryDumpProvider::instance());

    if (s_platform)
        s_platform->m_mainThread = 0;
    s_platform = 0;
}

Platform* Platform::current()
{
    return s_platform;
}

WebThread* Platform::mainThread() const
{
    return m_mainThread;
}

void Platform::registerMemoryDumpProvider(WebMemoryDumpProvider* provider, const char* name)
{
    WebMemoryDumpProviderAdapter* adapter = new WebMemoryDumpProviderAdapter(provider);
    ProviderToAdapterMap::AddResult result = memoryDumpProviders().add(provider, adoptPtr(adapter));
    if (!result.isNewEntry)
        return;
    adapter->set_is_registered(true);
    base::trace_event::MemoryDumpManager::GetInstance()->RegisterDumpProvider(adapter, name, base::ThreadTaskRunnerHandle::Get());
}

void Platform::unregisterMemoryDumpProvider(WebMemoryDumpProvider* provider)
{
    ProviderToAdapterMap::iterator it = memoryDumpProviders().find(provider);
    if (it == memoryDumpProviders().end())
        return;
    WebMemoryDumpProviderAdapter* adapter = it->value.get();
    base::trace_event::MemoryDumpManager::GetInstance()->UnregisterDumpProvider(adapter);
    adapter->set_is_registered(false);
    memoryDumpProviders().remove(it);
}

} // namespace blink
