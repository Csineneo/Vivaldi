// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/chromeos_pref_names.h"

namespace chromeos {
namespace prefs {

// A dictionary pref to hold the mute setting for all the currently known
// audio devices.
const char kAudioDevicesMute[] = "settings.audio.devices.mute";

// A dictionary pref storing the volume settings for all the currently known
// audio devices.
const char kAudioDevicesVolumePercent[] =
    "settings.audio.devices.volume_percent";

// An integer pref to initially mute volume if 1. This pref is ignored if
// |kAudioOutputAllowed| is set to false, but its value is preserved, therefore
// when the policy is lifted the original mute state is restored.  This setting
// is here only for migration purposes now. It is being replaced by the
// |kAudioDevicesMute| setting.
const char kAudioMute[] = "settings.audio.mute";

// A pref holding the value of the policy used to disable playing audio on
// ChromeOS devices. This pref overrides |kAudioMute| but does not overwrite
// it, therefore when the policy is lifted the original mute state is restored.
const char kAudioOutputAllowed[] = "hardware.audio_output_enabled";

// A double pref storing the user-requested volume. This setting is here only
// for migration purposes now. It is being replaced by the
// |kAudioDevicesVolumePercent| setting.
const char kAudioVolumePercent[] = "settings.audio.volume_percent";

}  // namespace prefs
}  // namespace chromeos

