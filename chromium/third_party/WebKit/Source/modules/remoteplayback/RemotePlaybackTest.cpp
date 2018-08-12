// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/remoteplayback/RemotePlayback.h"

#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "bindings/core/v8/V8BindingForTesting.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLVideoElement.h"
#include "core/testing/DummyPageHolder.h"
#include "platform/testing/UnitTestHelpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class MockFunction : public ScriptFunction {
 public:
  static MockFunction* create(ScriptState* scriptState) {
    return new MockFunction(scriptState);
  }

  v8::Local<v8::Function> bind() { return bindToV8Function(); }

  MOCK_METHOD1(call, ScriptValue(ScriptValue));

 private:
  explicit MockFunction(ScriptState* scriptState)
      : ScriptFunction(scriptState) {}
};

class RemotePlaybackTest : public ::testing::Test {
 protected:
  void cancelPrompt(RemotePlayback* remotePlayback) {
    remotePlayback->promptCancelled();
  }
};

TEST_F(RemotePlaybackTest, PromptCancelledRejectsWithNotAllowedError) {
  V8TestingScope scope;

  auto pageHolder = DummyPageHolder::create();

  HTMLMediaElement* element = HTMLVideoElement::create(pageHolder->document());
  RemotePlayback* remotePlayback = RemotePlayback::create(*element);

  MockFunction* resolve = MockFunction::create(scope.getScriptState());
  MockFunction* reject = MockFunction::create(scope.getScriptState());

  EXPECT_CALL(*resolve, call(::testing::_)).Times(0);
  EXPECT_CALL(*reject, call(::testing::_)).Times(1);

  remotePlayback->prompt(scope.getScriptState())
      .then(resolve->bind(), reject->bind());
  cancelPrompt(remotePlayback);
}

}  // namespace blink
