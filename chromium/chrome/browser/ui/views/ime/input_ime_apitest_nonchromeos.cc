// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_switches.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/dummy_text_input_client.h"
#include "ui/base/ime/input_method.h"

namespace extensions {

class InputImeApiTest : public ExtensionApiTest {
 public:
  InputImeApiTest() {}

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableInputImeAPI);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(InputImeApiTest);
};

IN_PROC_BROWSER_TEST_F(InputImeApiTest, CreateWindowTest) {
  // Manipulates the focused text input client because the follow cursor
  // window requires the text input focus.
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ui::InputMethod* input_method =
      browser_view->GetNativeWindow()->GetHost()->GetInputMethod();
  scoped_ptr<ui::DummyTextInputClient> client(
      new ui::DummyTextInputClient(ui::TEXT_INPUT_TYPE_TEXT));
  input_method->SetFocusedTextInputClient(client.get());

  ASSERT_TRUE(RunExtensionTest("input_ime_nonchromeos")) << message_;

  input_method->DetachTextInputClient(client.get());
}

}  // namespace extensions
