// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved.

#ifndef UI_VIVALDI_UI_UTILS_H_
#define UI_VIVALDI_UI_UTILS_H_

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/common/api/extension_types.h"

namespace vivaldi {
namespace ui_tools {

// Returns the currently active WebViewGuest.
extern extensions::WebViewGuest* GetActiveWebViewGuest();

// Returns the currently active WebViewGuest.
// |app_window| The app-window where the webviewguest is present.
// Used in paste and go and hardware key handling (ie. navigation buttons on
// mouse/keyboard)
extern extensions::WebViewGuest*
GetActiveWebViewGuest(extensions::NativeAppWindow *app_window);

// Returns the active AppWindow, currently used by progress updates to the
// taskbar on Windows.
extern extensions::AppWindow* GetActiveAppWindow();

extern extensions::WebViewGuest* GetActiveWebGuestFromBrowser(Browser* browser);

extern content::WebContents* GetWebContentsFromTabStrip(int tab_id,
                                                        Profile* profile);

extern bool IsOutsideAppWindow(int screen_x, int screen_y, Profile* profile);

extern bool EncodeBitmap(
    const SkBitmap& screen_capture,
    std::vector<unsigned char>* data,
    std::string& mime_type,
    extensions::api::extension_types::ImageFormat image_format,
    gfx::Size size,
    double scale,
    int image_quality,
    bool resize);

extern SkBitmap SmartCropAndSize(const SkBitmap& capture,
                                 int target_width,
                                 int target_height);

} // namespace ui_tools

} // vivaldi namespace

#endif // UI_VIVALDI_UI_UTILS_H_
