// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_CONTENT_SIMPLE_BROWSER_PUBLIC_CPP_MANIFEST_H_
#define SERVICES_CONTENT_SIMPLE_BROWSER_PUBLIC_CPP_MANIFEST_H_

#include "services/service_manager/public/cpp/manifest.h"

namespace simple_browser {

const service_manager::Manifest& GetManifest();

}  // namespace simple_browser

#endif  // SERVICES_CONTENT_SIMPLE_BROWSER_PUBLIC_CPP_MANIFEST_H_
