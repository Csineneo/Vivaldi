// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved

#include "extensions/vivaldi_script_dispatcher.h"

#include "app/vivaldi_apptools.h"
#include "extensions/renderer/module_system.h"
#include "extensions/renderer/script_context.h"

#include "vivaldi/grit/vivaldi_extension_resources.h"

namespace vivaldi {

// Called by Dispatcher::GetJsResources()
void VivaldiAddScriptResources(
    std::vector<std::pair<const char*, int> >* resources) {
  resources->push_back(std::make_pair("webViewPrivateMethods",
                                      IDR_WEB_VIEW_PRIVATE_API_METHODS_JS));
  resources->push_back(
      std::make_pair("webViewPrivate", IDR_WEB_VIEW_PRIVATE_JS));
  resources->push_back(
      std::make_pair("webViewEventsPrivate", IDR_WEB_VIEW_PRIVATE_EVENTS_JS));
  resources->push_back(std::make_pair("webViewAttributesPrivate",
                                      IDR_WEB_VIEW_PRIVATE_ATTRIBUTES_JS));
  resources->push_back(std::make_pair("webViewConstantsPrivate",
                                      IDR_WEB_VIEW_PRIVATE_CONSTANTS_JS));
}

// Called by Dispatcher::RequireGuestViewModules()
void VivaldiAddRequiredModules(extensions::ScriptContext* context,
                               extensions::ModuleSystem* module_system) {
  // Require WebView.
  if (context->GetAvailability("webViewInternal").is_available() &&
      vivaldi::IsVivaldiRunning()) {
    module_system->Require("webViewPrivateMethods");
  }
}

}  // namespace vivaldi
