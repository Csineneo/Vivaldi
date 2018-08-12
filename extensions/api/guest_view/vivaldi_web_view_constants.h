// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved

#ifndef EXTENSIONS_GUEST_VIEW_VIVALDI_WEB_VIEW_CONSTANTS_H
#define EXTENSIONS_GUEST_VIEW_VIVALDI_WEB_VIEW_CONSTANTS_H

namespace webview {

extern const char kEventRequestPageInfo[];
extern const char kEventSSLStateChanged[];
extern const char kEventTargetURLChanged[];
extern const char kEventCreateSearch[];
extern const char kEventMediaStateChanged[];
extern const char kEventPasteAndGo[];
extern const char kEventSimpleAction[];
extern const char kEventWebContentsDiscarded[];
extern const char kAttributeExtensionHost[];
extern const char kEventOnFullscreen[];
extern const char kEventContentBlocked[];

extern const char kNewSearchName[];
extern const char kNewSearchUrl[];
extern const char kClipBoardText[];
extern const char kPasteTarget[];
extern const char kModifiers[];
extern const char kLoadedBytes[];
extern const char kLoadedElements[];
extern const char kTotalElements[];
extern const char kGenCommand[];
extern const char kGenText[];
extern const char kGenUrl[];

} //namespace webview

#endif // EXTENSIONS_GUEST_VIEW_VIVALDI_WEB_VIEW_CONSTANTS_H
