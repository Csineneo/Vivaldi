// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PREVIEWS_CORE_PREVIEWS_EXPERIMENTS_H_
#define COMPONENTS_PREVIEWS_CORE_PREVIEWS_EXPERIMENTS_H_

#include "base/time/time.h"

namespace previews {

namespace params {

// The maximum number of recent previews navigations the black list looks at to
// determine if a host is blacklisted.
size_t MaxStoredHistoryLengthForBlackList();

// The maximum number of hosts allowed in the in memory black list.
size_t MaxInMemoryHostsInBlackList();

// The number of recent navigations that were opted out of that would trigger
// the host to be blacklisted.
int BlackListOptOutThreshold();

// The amount of time a host remains blacklisted due to opt outs.
base::TimeDelta BlackListDuration();

}  // namespace params

// Returns true if any client-side previews experiment is active.
bool IsIncludedInClientSidePreviewsExperimentsFieldTrial();

// Returns true if the field trial that should enable offline pages for
// prohibitvely slow networks is active.
bool IsOfflinePreviewsEnabled();

// Sets the appropriate state for field trial and variations to imitate the
// offline pages field trial.
bool EnableOfflinePreviewsForTesting();

}  // namespace previews

#endif  // COMPONENTS_PREVIEWS_CORE_PREVIEWS_EXPERIMENTS_H_
