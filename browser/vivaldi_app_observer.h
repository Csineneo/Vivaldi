// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved

#ifndef BROWSER_VIVALDI_APP_OBSERVER_H_
#define BROWSER_VIVALDI_APP_OBSERVER_H_

#import <Cocoa/Cocoa.h>

#include "chrome/browser/ui/browser.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"

class VivaldiBrowserWindow;

namespace vivaldi {

class VivaldiAppObserver : public extensions::BrowserContextKeyedAPI {
 public:
  explicit VivaldiAppObserver(content::BrowserContext* context);
  ~VivaldiAppObserver() override;

  static extensions::BrowserContextKeyedAPIFactory<VivaldiAppObserver>*
  GetFactoryInstance();

  // Convenience method to get the VivaldiAppObserver for a
  // content::BrowserContext.
  static VivaldiAppObserver* Get(content::BrowserContext* browser_context);

  void SetCommand(NSInteger tag, Browser* browser);

  // Called by VivaldiBrowserWindowCocoa when the window has been shown.
  void OnWindowShown(VivaldiBrowserWindow* window, bool was_hidden);

 private:
  friend class extensions::BrowserContextKeyedAPIFactory<VivaldiAppObserver>;

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() { return "VivaldiAppObserver"; }
  static const bool kServiceIsNULLWhileTesting = true;
  static const bool kServiceRedirectedInIncognito = true;

  NSInteger tag_ = 0;
  Browser* browser_ = nullptr;
  content::BrowserContext* browser_context_;

  DISALLOW_COPY_AND_ASSIGN(VivaldiAppObserver);
};

}  // namespace vivaldi

#endif  // BROWSER_VIVALDI_APP_OBSERVER_H_
