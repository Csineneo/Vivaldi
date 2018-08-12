// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved

#ifndef UI_DEVTOOLS_DEVTOOLS_CONNECTOR_H_
#define UI_DEVTOOLS_DEVTOOLS_CONNECTOR_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "chrome/browser/devtools/devtools_ui_bindings.h"
#include "content/public/browser/web_contents_delegate.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "net/cert/x509_certificate.h"

namespace content {
class BrowserContext;
}

namespace extensions {

class UIBindingsDelegate : public DevToolsUIBindings::Delegate {
 public:
  UIBindingsDelegate(content::BrowserContext* browser_context,
                     int tab_id,
                     DevToolsUIBindings::Delegate* delegate);
  ~UIBindingsDelegate() override;

  int tab_id() { return tab_id_; }

  // DevToolsUIBindings::Delegate implementation
  void ActivateWindow() override;
  void CloseWindow() override;
  void Inspect(scoped_refptr<content::DevToolsAgentHost> host) override;
  void SetInspectedPageBounds(const gfx::Rect& rect) override;
  void InspectElementCompleted() override;
  void SetIsDocked(bool is_docked) override;
  void OpenInNewTab(const std::string& url) override;
  void SetWhitelistedShortcuts(const std::string& message) override;
  void InspectedContentsClosing() override;
  void OnLoadCompleted() override;
  void OpenNodeFrontend() override;
  void ReadyForTest() override;
  InfoBarService* GetInfoBarService() override;
  void RenderProcessGone(bool crashed) override;

 private:
  // Notify JS side to update bounds.
  void NotifyUpdateBounds();

  // Original delegate owned by us.
  std::unique_ptr<DevToolsUIBindings::Delegate> ui_bindings_delegate_;

  int tab_id_ = 0;
  content::BrowserContext* browser_context_ = nullptr;
};

class DevtoolsConnectorItem
    : public content::WebContentsDelegate,
      public base::RefCounted<DevtoolsConnectorItem> {
 public:
  DevtoolsConnectorItem() = default;
  DevtoolsConnectorItem(int tab_id, content::BrowserContext* context);

  void set_guest_delegate(content::WebContentsDelegate* delegate) {
    guest_delegate_ = delegate;
  }
  void set_devtools_delegate(content::WebContentsDelegate* delegate) {
    devtools_delegate_ = delegate;
  }

  void set_ui_bindings_delegate(DevToolsUIBindings::Delegate* delegate) {
    // should only be set once.
    DCHECK(!connector_bindings_delegate_);

    connector_bindings_delegate_ =
        new UIBindingsDelegate(browser_context_, tab_id(), delegate);
  }

  const content::WebContentsDelegate* guest_delegate() {
    return guest_delegate_;
  }

  const content::WebContentsDelegate* devtoools_delegate() {
    return devtools_delegate_;
  }

  DevToolsUIBindings::Delegate* ui_bindings_delegate() {
    return connector_bindings_delegate_;
  }

  int tab_id() {
    return tab_id_;
  }

  void ResetDockingState();

  std::string docking_state() { return devtools_docking_state_; }
  void set_docking_state(std::string& docking_state) {
    devtools_docking_state_ = docking_state;
  }

  bool device_mode_enabled() { return device_mode_enabled_; }
  void set_device_mode_enabled(bool enabled) {
    device_mode_enabled_ = enabled;
  }

 private:
  friend class base::RefCounted<DevtoolsConnectorItem>;

  ~DevtoolsConnectorItem() override;

  // content::WebContentsDelegate:
  void ActivateContents(content::WebContents* contents) override;
  void AddNewContents(content::WebContents* source,
                      content::WebContents* new_contents,
                      WindowOpenDisposition disposition,
                      const gfx::Rect& initial_rect,
                      bool user_gesture,
                      bool* was_blocked) override;
  void WebContentsCreated(content::WebContents* source_contents,
                          int opener_render_process_id,
                          int opener_render_frame_id,
                          const std::string& frame_name,
                          const GURL& target_url,
                          content::WebContents* new_contents) override;
  void CloseContents(content::WebContents* source) override;
  void ContentsZoomChange(bool zoom_in) override;
  void BeforeUnloadFired(content::WebContents* tab,
                         bool proceed,
                         bool* proceed_to_fire_unload) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
                         content::WebContents* source,
                         const content::NativeWebKeyboardEvent& event) override;
  void HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) override;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
    content::WebContents* source) override;
  content::ColorChooser* OpenColorChooser(
    content::WebContents* web_contents,
    SkColor color,
    const std::vector<content::ColorSuggestion>& suggestions) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      const content::FileChooserParams& params) override;
  bool PreHandleGestureEvent(content::WebContents* source,
                             const blink::WebGestureEvent& event) override;
  void ShowCertificateViewerInDevTools(
    content::WebContents* web_contents,
    scoped_refptr<net::X509Certificate> certificate) override;

  // These are the original delegates Chromium would normally use
  // and we call into them to allow existing functionality to work.
  content::WebContentsDelegate* guest_delegate_ = nullptr;
  content::WebContentsDelegate* devtools_delegate_ = nullptr;

  int tab_id_ = 0;
  content::BrowserContext* browser_context_ = nullptr;

  // This will be owned by the DevtoolsUIBindings class and deleted there.
  UIBindingsDelegate* connector_bindings_delegate_ = nullptr;

  // Keeps track of the docking state per tab.
  std::string devtools_docking_state_ = "off";

  // Keeps track of the device mode state
  bool device_mode_enabled_ = false;
};

/*
This class controls the bridge delegates between the webview and the
DevtoolsWindow. Both classes needs to be set as a WebContentsDelegate,
so to handle that we assign that delegate using this class that will
delegate to both.
*/
class DevtoolsConnectorAPI : public BrowserContextKeyedAPI {
 public:
  explicit DevtoolsConnectorAPI(content::BrowserContext* context);
  ~DevtoolsConnectorAPI() override;

  // KeyedService implementation.
  void Shutdown() override;

  // BrowserContextKeyedAPI implementation.
  static BrowserContextKeyedAPIFactory<DevtoolsConnectorAPI>*
    GetFactoryInstance();

  DevtoolsConnectorItem* GetOrCreateDevtoolsConnectorItem(int tab_id);

  void RemoveDevtoolsConnectorItem(int tab_id);
  void CloseAllDevtools();

  static void BroadcastEvent(const std::string& eventname,
                             std::unique_ptr<base::ListValue> args,
                             content::BrowserContext* context);

  void SendOnUndockedEvent(content::BrowserContext* context,
                           int tab_id, bool show_window);

 private:
  friend class BrowserContextKeyedAPIFactory<DevtoolsConnectorAPI>;

  content::BrowserContext* browser_context_;

  // The guest view has ownership of the pointers contained within.
  std::vector<DevtoolsConnectorItem*> connector_items_;

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() {
    return "DevtoolsConnectorAPI";
  }
  static const bool kServiceIsNULLWhileTesting = true;
  static const bool kServiceRedirectedInIncognito = true;
};

}  // namespace extensions

#endif  // UI_DEVTOOLS_DEVTOOLS_CONNECTOR_H_
