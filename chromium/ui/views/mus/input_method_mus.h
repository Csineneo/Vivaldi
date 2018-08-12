// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ime/input_method_base.h"

#ifndef UI_VIEWS_MUS_INPUT_METHOD_MUS_H_
#define UI_VIEWS_MUS_INPUT_METHOD_MUS_H_

#include "base/macros.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/shell/public/cpp/connector.h"
#include "services/ui/public/interfaces/ime.mojom.h"
#include "ui/views/mus/mus_export.h"

namespace ui {
class Window;
}  // namespace mojo

namespace views {

class TextInputClientImpl;

class VIEWS_MUS_EXPORT InputMethodMus : public ui::InputMethodBase {
 public:
  InputMethodMus(ui::internal::InputMethodDelegate* delegate,
                 ui::Window* window);
  ~InputMethodMus() override;

  void Init(shell::Connector* connector);

  // Overridden from ui::InputMethod:
  void OnFocus() override;
  void OnBlur() override;
  bool OnUntranslatedIMEMessage(const base::NativeEvent& event,
                                NativeEventResult* result) override;
  void DispatchKeyEvent(ui::KeyEvent* event) override;
  void OnTextInputTypeChanged(const ui::TextInputClient* client) override;
  void OnCaretBoundsChanged(const ui::TextInputClient* client) override;
  void CancelComposition(const ui::TextInputClient* client) override;
  void OnInputLocaleChanged() override;
  bool IsCandidatePopupOpen() const override;

 private:
  friend TextInputClientImpl;

  // Overridden from ui::InputMethodBase:
  void OnDidChangeFocusedClient(ui::TextInputClient* focused_before,
                                ui::TextInputClient* focused) override;

  void UpdateTextInputType();

  // The toplevel window which is not owned by this class. This may be null
  // for tests.
  ui::Window* window_;

  ui::mojom::IMEServerPtr ime_server_;
  ui::mojom::InputMethodPtr input_method_;
  std::unique_ptr<TextInputClientImpl> text_input_client_;

  DISALLOW_COPY_AND_ASSIGN(InputMethodMus);
};

}  // namespace views

#endif  // UI_VIEWS_MUS_INPUT_METHOD_MUS_H_
