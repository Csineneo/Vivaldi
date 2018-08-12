// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/test_runner/layout_test_runtime_flags.h"

namespace test_runner {

LayoutTestRuntimeFlags::LayoutTestRuntimeFlags() {
  Reset();
}

void LayoutTestRuntimeFlags::Reset() {
  set_generate_pixel_results(true);

  set_dump_as_text(false);
  set_dump_child_frames_as_text(false);

  set_dump_as_markup(false);
  set_dump_child_frames_as_markup(false);

  set_dump_child_frame_scroll_positions(false);

  set_is_printing(false);

  set_policy_delegate_enabled(false);
  set_policy_delegate_is_permissive(false);
  set_policy_delegate_should_notify_done(false);
  set_wait_until_done(false);

  set_dump_selection_rect(false);
  set_dump_drag_image(false);

  set_accept_languages("");

  // No need to report the initial state - only the future delta is important.
  tracked_dictionary().ResetChangeTracking();
}

}  // namespace test_runner
