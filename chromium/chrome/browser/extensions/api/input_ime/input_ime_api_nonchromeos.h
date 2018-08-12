// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_INPUT_IME_INPUT_IME_API_NONCHROMEOS_H_
#define CHROME_BROWSER_EXTENSIONS_API_INPUT_IME_INPUT_IME_API_NONCHROMEOS_H_

#include "chrome/browser/extensions/api/input_ime/input_ime_event_router_base.h"
#include "chrome/browser/profiles/profile.h"

class Profile;

namespace extensions {
class InputImeEventRouterBase;

class InputImeEventRouter : public InputImeEventRouterBase {
 public:
  explicit InputImeEventRouter(Profile* profile);
  ~InputImeEventRouter() override;

  DISALLOW_COPY_AND_ASSIGN(InputImeEventRouter);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_INPUT_IME_INPUT_IME_API_NONCHROMEOS_H_
