// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_TABS_WINDOWS_UTIL_H__
#define CHROME_BROWSER_EXTENSIONS_API_TABS_WINDOWS_UTIL_H__

class UIThreadExtensionFunction;

namespace extensions {
class WindowController;
}

namespace windows_util {

// Populates |controller| for given |window_id|. If the window is not found,
// returns false and sets UIThreadExtensionFunction error_.
bool GetWindowFromWindowID(UIThreadExtensionFunction* function,
                           int window_id,
                           extensions::WindowController** controller);

// Returns true if |function| (and the profile and extension that it was
// invoked from) can operate on the window wrapped by |window_controller|.
bool CanOperateOnWindow(const UIThreadExtensionFunction* function,
                        const extensions::WindowController* controller);

}  // namespace windows_util

#endif  // CHROME_BROWSER_EXTENSIONS_API_TABS_WINDOWS_UTIL_H__
