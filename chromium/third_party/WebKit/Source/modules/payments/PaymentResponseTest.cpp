// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/payments/PaymentResponse.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptState.h"
#include "core/testing/DummyPageHolder.h"
#include "modules/payments/PaymentCompleter.h"
#include "modules/payments/PaymentDetailsTestHelper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/OwnPtr.h"
#include <utility>

namespace blink {
namespace {

class MockPaymentCompleter : public GarbageCollectedFinalized<MockPaymentCompleter>, public PaymentCompleter {
    USING_GARBAGE_COLLECTED_MIXIN(MockPaymentCompleter);
    WTF_MAKE_NONCOPYABLE(MockPaymentCompleter);

public:
    MockPaymentCompleter()
    {
        ON_CALL(*this, complete(testing::_, testing::_))
            .WillByDefault(testing::ReturnPointee(&m_dummyPromise));
    }

    ~MockPaymentCompleter() override {}

    MOCK_METHOD2(complete, ScriptPromise(ScriptState*, bool success));

    DEFINE_INLINE_TRACE() {}

private:
    ScriptPromise m_dummyPromise;
};

class PaymentResponseTest : public testing::Test {
public:
    PaymentResponseTest()
        : m_page(DummyPageHolder::create())
    {
        m_page->document().setSecurityOrigin(SecurityOrigin::create(KURL(KURL(), "https://www.example.com/")));
    }

    ~PaymentResponseTest() override {}

    ScriptState* getScriptState() { return ScriptState::forMainWorld(m_page->document().frame()); }
    ExceptionState& getExceptionState() { return m_exceptionState; }

private:
    OwnPtr<DummyPageHolder> m_page;
    NonThrowableExceptionState m_exceptionState;
};

TEST_F(PaymentResponseTest, DataCopiedOver)
{
    mojom::wtf::PaymentResponsePtr input = mojom::wtf::PaymentResponse::New();
    input->method_name = "foo";
    input->stringified_details = "{\"transactionId\": 123}";
    MockPaymentCompleter* completeCallback = new MockPaymentCompleter;

    PaymentResponse output(std::move(input), completeCallback);

    // TODO(rouslan): Verify that output.details() contains parsed input->stringified_details.
    EXPECT_FALSE(getExceptionState().hadException());
    EXPECT_EQ("foo", output.methodName());
}

TEST_F(PaymentResponseTest, CompleteCalled)
{
    mojom::wtf::PaymentResponsePtr input = mojom::wtf::PaymentResponse::New();
    input->method_name = "foo";
    input->stringified_details = "{\"transactionId\": 123}";
    MockPaymentCompleter* completeCallback = new MockPaymentCompleter;
    PaymentResponse output(std::move(input), completeCallback);

    EXPECT_FALSE(getExceptionState().hadException());
    EXPECT_CALL(*completeCallback, complete(getScriptState(), true));

    output.complete(getScriptState(), true);
}

} // namespace
} // namespace blink
