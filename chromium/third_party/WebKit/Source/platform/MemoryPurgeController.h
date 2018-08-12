// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MemoryPurgeController_h
#define MemoryPurgeController_h

#include "platform/PlatformExport.h"
#include "platform/Timer.h"
#include "platform/heap/Handle.h"
#include "public/platform/WebMemoryPressureLevel.h"

namespace blink {

enum class DeviceKind {
    NotSpecified,
    LowEnd,
};

// Classes which have discardable/reducible memory can implement this
// interface to be informed when they should reduce memory consumption.
// MemoryPurgeController assumes that subclasses of MemoryPurgeClient are
// WillBes.
class PLATFORM_EXPORT MemoryPurgeClient : public GarbageCollectedMixin {
public:
    virtual ~MemoryPurgeClient() { }

    // MemoryPurgeController invokes this callback when a memory purge event
    // has occurred.
    virtual void purgeMemory(DeviceKind) = 0;

    DECLARE_VIRTUAL_TRACE();
};

// MemoryPurgeController listens to some events which could be opportunities
// for reducing memory consumption and notifies its clients.
// Since we want to control memory per tab, MemoryPurgeController is owned by
// Page.
class PLATFORM_EXPORT MemoryPurgeController final : public GarbageCollected<MemoryPurgeController> {
    WTF_MAKE_NONCOPYABLE(MemoryPurgeController);
public:
    static void onMemoryPressure(WebMemoryPressureLevel);

    static MemoryPurgeController* create()
    {
        return new MemoryPurgeController;
    }

    void registerClient(MemoryPurgeClient* client)
    {
        ASSERT(isMainThread());
        ASSERT(client);
        ASSERT(!m_clients.contains(client));
        m_clients.add(client);
    }

    void unregisterClient(MemoryPurgeClient* client)
    {
        // Don't assert m_clients.contains() so that clients can unregister
        // unconditionally.
        ASSERT(isMainThread());
        m_clients.remove(client);
    }

    void purgeMemory();

    DECLARE_TRACE();

private:
    MemoryPurgeController();

    HeapHashSet<WeakMember<MemoryPurgeClient>> m_clients;
    DeviceKind m_deviceKind;
};

} // namespace blink

#endif // MemoryPurgeController_h
