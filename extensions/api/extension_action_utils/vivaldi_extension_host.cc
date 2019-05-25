// Copyright 2019 Vivaldi Technologies AS. All rights reserved.

#include "extensions/api/extension_action_utils/vivaldi_extension_host.h"

#include "extensions/browser/extension_host_delegate.h"
#include "extensions/browser/extension_web_contents_observer.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/view_type_utils.h"

namespace vivaldi {

VivaldiExtensionHost::VivaldiExtensionHost(
    content::BrowserContext* browser_context,
    extensions::ViewType host_type,
    content::WebContents* webcontents)
    : delegate_(extensions::ExtensionsBrowserClient::Get()
                    ->CreateExtensionHostDelegate()) {
  SetViewType(webcontents, host_type);

  // Set up web contents observers and pref observers.
  delegate_->OnExtensionHostCreated(webcontents);

  extensions::ExtensionWebContentsObserver::GetForWebContents(webcontents)
      ->dispatcher()
      ->set_delegate(this);
}

VivaldiExtensionHost::~VivaldiExtensionHost() {}

}  // namespace vivaldi
