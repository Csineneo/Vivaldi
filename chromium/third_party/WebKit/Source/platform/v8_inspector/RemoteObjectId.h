// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RemoteObjectId_h
#define RemoteObjectId_h

#include "wtf/Allocator.h"
#include "wtf/Forward.h"

namespace blink {

namespace protocol {
class DictionaryValue;
}

class RemoteObjectIdBase {
    USING_FAST_MALLOC(RemoteObjectIdBase);
public:
    int contextId() const { return m_injectedScriptId; }

protected:
    RemoteObjectIdBase();
    ~RemoteObjectIdBase() { }

    PassRefPtr<protocol::DictionaryValue> parseInjectedScriptId(const String&);

    int m_injectedScriptId;
};

class RemoteObjectId final : public RemoteObjectIdBase {
public:
    static PassOwnPtr<RemoteObjectId> parse(const String&);
    ~RemoteObjectId() { }
    int id() const { return m_id; }

private:
    RemoteObjectId();

    int m_id;
};

class RemoteCallFrameId final : public RemoteObjectIdBase {
public:
    static PassOwnPtr<RemoteCallFrameId> parse(const String&);
    ~RemoteCallFrameId() { }

    int frameOrdinal() const { return m_frameOrdinal; }
    int asyncStackOrdinal() const { return m_asyncStackOrdinal; }

private:
    RemoteCallFrameId();

    int m_frameOrdinal;
    int m_asyncStackOrdinal;
};

} // namespace blink

#endif // !defined(RemoteObjectId_h)
