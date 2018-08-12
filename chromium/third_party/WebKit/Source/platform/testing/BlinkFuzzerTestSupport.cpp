// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/testing/BlinkFuzzerTestSupport.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "mojo/edk/embedder/embedder.h"
#include "platform/weborigin/SchemeRegistry.h"
#include <content/test/blink_test_environment.h>

namespace blink {

void InitializeBlinkFuzzTest(int* argc, char*** argv) {
  // Note: we don't tear anything down here after an iteration of the fuzzer
  // is complete, this is for efficiency. We rerun the fuzzer with the same
  // environment as the previous iteration.
  base::AtExitManager atExit;

  mojo::edk::Init();
  base::CommandLine::Init(*argc, *argv);

  content::SetUpBlinkTestEnvironment();

  blink::SchemeRegistry::initialize();
}

}  // namespace blink
