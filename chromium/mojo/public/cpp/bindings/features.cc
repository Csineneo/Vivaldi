// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/features.h"

namespace mojo {
namespace features {

// Enables a task to be scheduled for each individual message dispatched to a
// Mojo binding endpoint (or reply to an InterfacePtr).
//
// When disabled, dispatch happens eagerly in batch, so when a binding is
// scheduled to dispatch messages, it fully flushes and dispatches all queued
// messages within the extent of a single scheduler task.
//
// Enabling this feature allows for more fine-grained performance control
// through the scheduler, but may initially cause some important edge cases to
// regress in performance due to high-priority messages seeing increased
// latency. Ideally we'd address these cases by giving the affected bindings
// higher-priority TaskRunners.
const base::Feature kTaskPerMessage{"MojoTaskPerMessage",
                                    base::FEATURE_ENABLED_BY_DEFAULT};

}  // namespace features
}  // namespace mojo
