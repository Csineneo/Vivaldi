// Copyright (c) 2016 Vivaldi. All rights reserved.

#include "browser/vivaldi_clipboard_utils.h"

#include "third_party/WebKit/public/platform/WebMouseEvent.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace vivaldi {

namespace clipboard {

bool suppress_selection_write = true;

// Updates 'suppress_selection_write' for each event.
void OnInputEvent(const blink::WebInputEvent& input_event) {
  if (input_event.type() == blink::WebInputEvent::MouseMove) {
    // Never set to true here to allow mouse multiclicking work the best.
    if ((input_event.modifiers() & blink::WebInputEvent::LeftButtonDown))
      suppress_selection_write = false;
  } else if (input_event.type() == blink::WebInputEvent::MouseDown) {
    blink::WebMouseEvent& event = *((blink::WebMouseEvent*)&input_event);
    suppress_selection_write = event.clickCount < 2;
  } else if (input_event.type() == blink::WebInputEvent::RawKeyDown &&
             (input_event.modifiers() & blink::WebInputEvent::ShiftKey)) {
    blink::WebKeyboardEvent& event =*((blink::WebKeyboardEvent*)&input_event);
    suppress_selection_write =
        !(event.windowsKeyCode == ui::VKEY_LEFT ||
          event.windowsKeyCode == ui::VKEY_RIGHT ||
          event.windowsKeyCode == ui::VKEY_UP ||
          event.windowsKeyCode == ui::VKEY_DOWN ||
          event.windowsKeyCode == ui::VKEY_HOME ||
          event.windowsKeyCode == ui::VKEY_END ||
          event.windowsKeyCode == ui::VKEY_PRIOR ||
          event.windowsKeyCode == ui::VKEY_NEXT);
  } else if (input_event.type() == blink::WebInputEvent::RawKeyDown &&
             (input_event.modifiers() & blink::WebInputEvent::ControlKey)) {
    blink::WebKeyboardEvent& event =*((blink::WebKeyboardEvent*)&input_event);
    // NOTE(espen). We probably want to make this configurable
    // Ctrl+A: Select All.
    suppress_selection_write = event.windowsKeyCode != ui::VKEY_A;
  } else if (input_event.type() == blink::WebInputEvent::Char) {
    // Do nothing. Wait for KeyUp to set suppress_selection_write to true.
  } else {
    suppress_selection_write = true;
  }
}

bool SuppressWrite(ui::ClipboardType clipboardType) {
  if (clipboardType == ui::CLIPBOARD_TYPE_SELECTION) {
    return suppress_selection_write;
  }
  return false;
}

}  // Clipboard

}  // vivaldi