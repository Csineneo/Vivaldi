// Copyright 2015-2016 Vivaldi Technologies AS. All rights reserved.

#ifndef EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_UTILS_API_H_
#define EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_UTILS_API_H_

#include <map>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/browser/extensions/component_migration_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_delegate.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "extensions/browser/extension_icon_image.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/schema/browser_action_utilities.h"

namespace extensions {

class ExtensionActionUtil;

class ExtensionActionUtilFactory : public BrowserContextKeyedServiceFactory {
 public:
  static ExtensionActionUtil* GetForProfile(Profile* profile);

  static ExtensionActionUtilFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<ExtensionActionUtilFactory>;

  ExtensionActionUtilFactory();
  ~ExtensionActionUtilFactory() override;

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
  bool ServiceIsNULLWhileTesting() const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

// A class observing being an observer on ExtensionActionAPI

class ExtensionActionUtil
    : public KeyedService,
      public extensions::ExtensionActionAPI::Observer,
      public extensions::ExtensionRegistryObserver,
      public TabStripModelObserver,
      public extensions::ComponentMigrationHelper::ComponentActionDelegate,
      public chrome::BrowserListObserver,
      public ToolbarActionViewDelegate {
  friend struct base::DefaultSingletonTraits<ExtensionActionUtil>;
  Profile* profile_;

  ScopedObserver<extensions::ExtensionRegistry,
                 extensions::ExtensionRegistryObserver>
      extension_registry_observer_;

  ScopedObserver<ExtensionActionAPI, ExtensionActionAPI::Observer>
      extension_action_api_observer_;

  std::unique_ptr<extensions::ComponentMigrationHelper>
      component_migration_helper_;

  void OnImageLoaded(const std::string& extension_id, const gfx::Image& image);

  // Component extensions is first added and removed and then added to this set
  // separately.
  std::set<std::string> component_extension_actions_;

  content::WebContents* current_webcontents_ = nullptr;

 public:
  static void BroadcastEvent(const std::string& eventname,
                             std::unique_ptr<base::ListValue> args,
                             content::BrowserContext* context);

  // Implementing ExtensionActionAPI::Observer.
  void OnExtensionActionUpdated(
      ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) override;

  // Called when there is a change to the extension action's visibility.
  void OnExtensionActionVisibilityChanged(const std::string& extension_id,
                                          bool is_now_visible) override;

  // Called when the page actions have been refreshed do to a possible change
  // in count or visibility.
  void OnPageActionsUpdated(content::WebContents* web_contents) override;

  // Called when the ExtensionActionAPI is shutting down, giving observers a
  // chance to unregister themselves if there is not a definitive lifecycle.
  void OnExtensionActionAPIShuttingDown() override;

  // Overridden from extensions::ExtensionRegistryObserver:
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(
      content::BrowserContext* browser_context,
      const extensions::Extension* extension,
      extensions::UnloadedExtensionInfo::Reason reason) override;

  // Overridden from TabStripModelObserver:
  void ActiveTabChanged(content::WebContents* old_contents,
                        content::WebContents* new_contents,
                        int index,
                        int reason) override;

  // ComponentMigrationHelper::ComponentActionDelegate:
  void AddComponentAction(const std::string& action_id) override;
  void RemoveComponentAction(const std::string& action_id) override;
  bool HasComponentAction(const std::string& action_id) const override;

  // BrowserListObserver
  void OnBrowserAdded(Browser* browser) override;

  // ToolbarActionViewDelegate
  content::WebContents* GetCurrentWebContents() const override;

  // Updates the view to reflect current state.
  void UpdateState() override;

  // Returns true if a context menu is running.
  bool IsMenuRunning() const override;

  extensions::ComponentMigrationHelper* component_migration_helper() {
    return component_migration_helper_.get();
  }

  // Fills the relevant information about an extensionaction for a specific tab.
  // return true if the action should be added.
  static bool FillInfoForTabId(
      vivaldi::extension_action_utils::ExtensionInfo& info,
      ExtensionAction* action,
      int tab_id,
      Profile* profile);

  static bool FillInfoFromComponentExtension(
      const std::string* action_id,
      vivaldi::extension_action_utils::ExtensionInfo& info,
      Profile* profile);

  static void FillInfoFromManifest(
      vivaldi::extension_action_utils::ExtensionInfo& info,
      const Extension* extension);

  static bool GetWindowIdFromExtData(const std::string& extdata,
                                     std::string& windowId);

  // Encodes the passed bitmap as a PNG represented as a dataurl.
  static std::string* EncodeBitmapToPng(const SkBitmap* bitmap);

  std::set<std::string> component_extension_actions() {
    return component_extension_actions_;
  }

  void set_current_webcontents(content::WebContents* contents) {
    current_webcontents_ = contents;
  }

  ExtensionActionUtil(Profile*);
  ~ExtensionActionUtil() override;

  base::WeakPtrFactory<ExtensionActionUtil> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionActionUtil);
};

class ExtensionActionUtilsGetToolbarExtensionsFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("extensionActionUtils.getToolbarExtensions",
                             GETTOOLBAR_EXTENSIONS)
  ExtensionActionUtilsGetToolbarExtensionsFunction();

 protected:
  ~ExtensionActionUtilsGetToolbarExtensionsFunction() override;

  // ExtensionFunction:
  bool RunAsync() override;
  DISALLOW_COPY_AND_ASSIGN(ExtensionActionUtilsGetToolbarExtensionsFunction);
};

class ExtensionActionUtilsExecuteExtensionActionFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("extensionActionUtils.executeExtensionAction",
                             EXECUTE_EXTENSIONACTION)
  ExtensionActionUtilsExecuteExtensionActionFunction();

 protected:
  ~ExtensionActionUtilsExecuteExtensionActionFunction() override;

  // ExtensionFunction:
  bool RunAsync() override;

  DISALLOW_COPY_AND_ASSIGN(ExtensionActionUtilsExecuteExtensionActionFunction);
};

class ExtensionActionUtilsToggleBrowserActionVisibilityFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION(
      "extensionActionUtils.toggleBrowserActionVisibility",
      TOGGLE_BROWSERACTIONVISIBILITY)
  ExtensionActionUtilsToggleBrowserActionVisibilityFunction();

 protected:
  ~ExtensionActionUtilsToggleBrowserActionVisibilityFunction() override;

  // ExtensionFunction:
  bool RunAsync() override;

  DISALLOW_COPY_AND_ASSIGN(
      ExtensionActionUtilsToggleBrowserActionVisibilityFunction);
};

class ExtensionActionUtilsRemoveExtensionFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("extensionActionUtils.removeExtension",
                             GETTOOLBAR_EXTENSIONS)
  ExtensionActionUtilsRemoveExtensionFunction();

 protected:
  ~ExtensionActionUtilsRemoveExtensionFunction() override;

  // ExtensionFunction:
  bool RunAsync() override;

  DISALLOW_COPY_AND_ASSIGN(ExtensionActionUtilsRemoveExtensionFunction);
};

}  // namespace extensions

#endif  // EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_UTILS_API_H_
