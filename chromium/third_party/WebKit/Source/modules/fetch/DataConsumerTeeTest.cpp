// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/fetch/DataConsumerTee.h"

#include "core/testing/DummyPageHolder.h"
#include "core/testing/NullExecutionContext.h"
#include "modules/fetch/DataConsumerHandleTestUtil.h"
#include "platform/ThreadSafeFunctional.h"
#include "platform/WaitableEvent.h"
#include "platform/WebThreadSupportingGC.h"
#include "public/platform/Platform.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebTraceLocation.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"
#include <string.h>
#include <v8.h>

namespace blink {
namespace {

using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using Checkpoint = StrictMock<::testing::MockFunction<void(int)>>;

using Result = WebDataConsumerHandle::Result;
using Thread = DataConsumerHandleTestUtil::Thread;
const Result kDone = WebDataConsumerHandle::Done;
const Result kUnexpectedError = WebDataConsumerHandle::UnexpectedError;
const FetchDataConsumerHandle::Reader::BlobSizePolicy kDisallowBlobWithInvalidSize = FetchDataConsumerHandle::Reader::DisallowBlobWithInvalidSize;
const FetchDataConsumerHandle::Reader::BlobSizePolicy kAllowBlobWithInvalidSize = FetchDataConsumerHandle::Reader::AllowBlobWithInvalidSize;

using Command = DataConsumerHandleTestUtil::Command;
using Handle = DataConsumerHandleTestUtil::ReplayingHandle;
using HandleReader = DataConsumerHandleTestUtil::HandleReader;
using HandleTwoPhaseReader = DataConsumerHandleTestUtil::HandleTwoPhaseReader;
using HandleReadResult = DataConsumerHandleTestUtil::HandleReadResult;
using MockFetchDataConsumerHandle = DataConsumerHandleTestUtil::MockFetchDataConsumerHandle;
using MockFetchDataConsumerReader = DataConsumerHandleTestUtil::MockFetchDataConsumerReader;
template <typename T>
using HandleReaderRunner = DataConsumerHandleTestUtil::HandleReaderRunner<T>;

String toString(const Vector<char>& v)
{
    return String(v.data(), v.size());
}

template<typename Handle>
class TeeCreationThread {
public:
    void run(PassOwnPtr<Handle> src, OwnPtr<Handle>* dest1, OwnPtr<Handle>* dest2)
    {
        m_thread = adoptPtr(new Thread("src thread", Thread::WithExecutionContext));
        m_waitableEvent = adoptPtr(new WaitableEvent());
        m_thread->thread()->postTask(BLINK_FROM_HERE, threadSafeBind(&TeeCreationThread<Handle>::runInternal, AllowCrossThreadAccess(this), passed(std::move(src)), AllowCrossThreadAccess(dest1), AllowCrossThreadAccess(dest2)));
        m_waitableEvent->wait();
    }

    Thread* getThread() { return m_thread.get(); }

private:
    void runInternal(PassOwnPtr<Handle> src, OwnPtr<Handle>* dest1, OwnPtr<Handle>* dest2)
    {
        DataConsumerTee::create(m_thread->getExecutionContext(), std::move(src), dest1, dest2);
        m_waitableEvent->signal();
    }

    OwnPtr<Thread> m_thread;
    OwnPtr<WaitableEvent> m_waitableEvent;
};

TEST(DataConsumerTeeTest, CreateDone)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Done));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleReader> r1(std::move(dest1)), r2(std::move(dest2));

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kDone, res1->result());
    EXPECT_EQ(0u, res1->data().size());
    EXPECT_EQ(kDone, res2->result());
    EXPECT_EQ(0u, res2->data().size());
}

TEST(DataConsumerTeeTest, Read)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Wait));
    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Data, "world"));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Done));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleReader> r1(std::move(dest1));
    HandleReaderRunner<HandleReader> r2(std::move(dest2));

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kDone, res1->result());
    EXPECT_EQ("hello, world", toString(res1->data()));

    EXPECT_EQ(kDone, res2->result());
    EXPECT_EQ("hello, world", toString(res2->data()));
}

TEST(DataConsumerTeeTest, TwoPhaseRead)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Wait));
    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Data, "world"));
    src->add(Command(Command::Wait));
    src->add(Command(Command::Done));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleTwoPhaseReader> r1(std::move(dest1));
    HandleReaderRunner<HandleTwoPhaseReader> r2(std::move(dest2));

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kDone, res1->result());
    EXPECT_EQ("hello, world", toString(res1->data()));

    EXPECT_EQ(kDone, res2->result());
    EXPECT_EQ("hello, world", toString(res2->data()));
}

TEST(DataConsumerTeeTest, Error)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Data, "world"));
    src->add(Command(Command::Error));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleReader> r1(std::move(dest1));
    HandleReaderRunner<HandleReader> r2(std::move(dest2));

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kUnexpectedError, res1->result());
    EXPECT_EQ(kUnexpectedError, res2->result());
}

void postStop(Thread* thread)
{
    thread->getExecutionContext()->stopActiveDOMObjects();
}

TEST(DataConsumerTeeTest, StopSource)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Data, "world"));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleReader> r1(std::move(dest1));
    HandleReaderRunner<HandleReader> r2(std::move(dest2));

    // We can pass a raw pointer because the subsequent |wait| calls ensure
    // t->thread() is alive.
    t->getThread()->thread()->postTask(BLINK_FROM_HERE, threadSafeBind(postStop, AllowCrossThreadAccess(t->getThread())));

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kUnexpectedError, res1->result());
    EXPECT_EQ(kUnexpectedError, res2->result());
}

TEST(DataConsumerTeeTest, DetachSource)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Data, "world"));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleReader> r1(std::move(dest1));
    HandleReaderRunner<HandleReader> r2(std::move(dest2));

    t = nullptr;

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kUnexpectedError, res1->result());
    EXPECT_EQ(kUnexpectedError, res2->result());
}

TEST(DataConsumerTeeTest, DetachSourceAfterReadingDone)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Data, "world"));
    src->add(Command(Command::Done));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    HandleReaderRunner<HandleReader> r1(std::move(dest1));
    OwnPtr<HandleReadResult> res1 = r1.wait();

    EXPECT_EQ(kDone, res1->result());
    EXPECT_EQ("hello, world", toString(res1->data()));

    t = nullptr;

    HandleReaderRunner<HandleReader> r2(std::move(dest2));
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kDone, res2->result());
    EXPECT_EQ("hello, world", toString(res2->data()));
}

TEST(DataConsumerTeeTest, DetachOneDestination)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Data, "world"));
    src->add(Command(Command::Done));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    dest1 = nullptr;

    HandleReaderRunner<HandleReader> r2(std::move(dest2));
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kDone, res2->result());
    EXPECT_EQ("hello, world", toString(res2->data()));
}

TEST(DataConsumerTeeTest, DetachBothDestinationsShouldStopSourceReader)
{
    OwnPtr<Handle> src(Handle::create());
    RefPtr<Handle::Context> context(src->getContext());
    OwnPtr<WebDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Data, "hello, "));
    src->add(Command(Command::Data, "world"));

    OwnPtr<TeeCreationThread<WebDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<WebDataConsumerHandle>());
    t->run(std::move(src), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    dest1 = nullptr;
    dest2 = nullptr;

    // Collect garbage to finalize the source reader.
    ThreadHeap::collectAllGarbage();
    context->detached()->wait();
}

TEST(FetchDataConsumerTeeTest, Create)
{
    RefPtr<BlobDataHandle> blobDataHandle = BlobDataHandle::create();
    OwnPtr<MockFetchDataConsumerHandle> src(MockFetchDataConsumerHandle::create());
    OwnPtr<MockFetchDataConsumerReader> reader(MockFetchDataConsumerReader::create());

    Checkpoint checkpoint;
    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*src, obtainReaderInternal(_)).WillOnce(Return(reader.get()));
    EXPECT_CALL(*reader, drainAsBlobDataHandle(kAllowBlobWithInvalidSize)).WillOnce(Return(blobDataHandle));
    EXPECT_CALL(*reader, destruct());
    EXPECT_CALL(checkpoint, Call(2));

    // |reader| is adopted by |obtainReader|.
    ASSERT_TRUE(reader.leakPtr());

    OwnPtr<FetchDataConsumerHandle> dest1, dest2;
    OwnPtr<TeeCreationThread<FetchDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<FetchDataConsumerHandle>());

    checkpoint.Call(1);
    t->run(std::move(src), &dest1, &dest2);
    checkpoint.Call(2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);
    EXPECT_EQ(blobDataHandle, dest1->obtainReader(nullptr)->drainAsBlobDataHandle(kAllowBlobWithInvalidSize));
    EXPECT_EQ(blobDataHandle, dest2->obtainReader(nullptr)->drainAsBlobDataHandle(kAllowBlobWithInvalidSize));
}

TEST(FetchDataConsumerTeeTest, CreateFromBlobWithInvalidSize)
{
    RefPtr<BlobDataHandle> blobDataHandle = BlobDataHandle::create(BlobData::create(), -1);
    OwnPtr<MockFetchDataConsumerHandle> src(MockFetchDataConsumerHandle::create());
    OwnPtr<MockFetchDataConsumerReader> reader(MockFetchDataConsumerReader::create());

    Checkpoint checkpoint;
    InSequence s;
    EXPECT_CALL(checkpoint, Call(1));
    EXPECT_CALL(*src, obtainReaderInternal(_)).WillOnce(Return(reader.get()));
    EXPECT_CALL(*reader, drainAsBlobDataHandle(kAllowBlobWithInvalidSize)).WillOnce(Return(blobDataHandle));
    EXPECT_CALL(*reader, destruct());
    EXPECT_CALL(checkpoint, Call(2));

    // |reader| is adopted by |obtainReader|.
    ASSERT_TRUE(reader.leakPtr());

    OwnPtr<FetchDataConsumerHandle> dest1, dest2;
    OwnPtr<TeeCreationThread<FetchDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<FetchDataConsumerHandle>());

    checkpoint.Call(1);
    t->run(std::move(src), &dest1, &dest2);
    checkpoint.Call(2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);
    EXPECT_FALSE(dest1->obtainReader(nullptr)->drainAsBlobDataHandle(kDisallowBlobWithInvalidSize));
    EXPECT_EQ(blobDataHandle, dest1->obtainReader(nullptr)->drainAsBlobDataHandle(kAllowBlobWithInvalidSize));
    EXPECT_FALSE(dest2->obtainReader(nullptr)->drainAsBlobDataHandle(kDisallowBlobWithInvalidSize));
    EXPECT_EQ(blobDataHandle, dest2->obtainReader(nullptr)->drainAsBlobDataHandle(kAllowBlobWithInvalidSize));
}

TEST(FetchDataConsumerTeeTest, CreateDone)
{
    OwnPtr<Handle> src(Handle::create());
    OwnPtr<FetchDataConsumerHandle> dest1, dest2;

    src->add(Command(Command::Done));

    OwnPtr<TeeCreationThread<FetchDataConsumerHandle>> t = adoptPtr(new TeeCreationThread<FetchDataConsumerHandle>());
    t->run(createFetchDataConsumerHandleFromWebHandle(std::move(src)), &dest1, &dest2);

    ASSERT_TRUE(dest1);
    ASSERT_TRUE(dest2);

    EXPECT_FALSE(dest1->obtainReader(nullptr)->drainAsBlobDataHandle(kAllowBlobWithInvalidSize));
    EXPECT_FALSE(dest2->obtainReader(nullptr)->drainAsBlobDataHandle(kAllowBlobWithInvalidSize));

    HandleReaderRunner<HandleReader> r1(std::move(dest1)), r2(std::move(dest2));

    OwnPtr<HandleReadResult> res1 = r1.wait();
    OwnPtr<HandleReadResult> res2 = r2.wait();

    EXPECT_EQ(kDone, res1->result());
    EXPECT_EQ(0u, res1->data().size());
    EXPECT_EQ(kDone, res2->result());
    EXPECT_EQ(0u, res2->data().size());
}

} // namespace
} // namespace blink
