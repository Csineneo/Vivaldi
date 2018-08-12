// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/common/mojo_channel_switches.h"

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "ipc/mojo/ipc_channel_mojo.h"
#include "mojo/common/common_type_converters.h"

namespace switches {

// Enable ChannelMojo on any supported platform.
const char kEnableMojoChannel[] = "enable-mojo-channel";

// The token to use to construct the message pipe on which to layer ChannelMojo.
const char kMojoChannelToken[] = "mojo-channel-token";

}  // namespace switches

namespace {

const char kMojoChannelExperimentName[] = "MojoChannel";

}  // namespace

namespace content {

bool ShouldUseMojoChannel() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableMojoChannel))
    return true;

  const std::string group =
      base::FieldTrialList::FindFullName(kMojoChannelExperimentName);
  if (group == "Enabled")
    return true;

  return false;
}

}  // namespace content
