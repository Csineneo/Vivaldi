// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/win/mf_initializer.h"

#include <mfapi.h>

#include "base/logging.h"
#include "base/win/windows_version.h"

namespace media {

// Media Foundation version number has last changed with Windows 7, see
// mfapi.h.
const int kMFVersionVista = (0x0001 << 16 | MF_API_VERSION);
const int kMFVersionWin7 = (0x0002 << 16 | MF_API_VERSION);

void InitializeMediaFoundation() {
  static HRESULT result = MFStartup(base::win::GetVersion() >= base::win::VERSION_WIN7
                                  ? kMFVersionWin7
                                  : kMFVersionVista,
                              MFSTARTUP_LITE);
  DCHECK_EQ(result, S_OK);
}

}  // namespace media
