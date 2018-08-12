// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/input_method/input_method_engine_base.h"

#undef FocusIn
#undef FocusOut
#undef RootWindow
#include <algorithm>
#include <map>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/ime_bridge.h"
#include "ui/base/ime/text_input_flags.h"
#include "ui/events/event.h"
#include "ui/events/event_processor.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_util.h"

#if defined(OS_CHROMEOS)
#include "ui/base/ime/chromeos/ime_keymap.h"
#elif defined(OS_WIN)
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_codes_win.h"
#elif defined(OS_LINUX)
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#endif

namespace input_method {

namespace {

const char kErrorNotActive[] = "IME is not active";
const char kErrorWrongContext[] = "Context is not active";

// Notifies InputContextHandler that the composition is changed.
void UpdateComposition(const ui::CompositionText& composition_text,
                       uint32_t cursor_pos,
                       bool is_visible) {
  ui::IMEInputContextHandlerInterface* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();
  if (input_context)
    input_context->UpdateCompositionText(composition_text, cursor_pos,
                                         is_visible);
}

// Returns the length of characters of a UTF-8 string with unknown string
// length. Cannot apply faster algorithm to count characters in an utf-8
// string without knowing the string length,  so just does a full scan.
size_t GetUtf8StringLength(const char* s) {
  size_t ret = 0;
  while (*s) {
    if ((*s & 0xC0) != 0x80)
      ret++;
    ++s;
  }
  return ret;
}

#if defined(OS_CHROMEOS)
std::string GetKeyFromEvent(const ui::KeyEvent& event) {
  const std::string code = event.GetCodeString();
  if (base::StartsWith(code, "Control", base::CompareCase::SENSITIVE))
    return "Ctrl";
  if (base::StartsWith(code, "Shift", base::CompareCase::SENSITIVE))
    return "Shift";
  if (base::StartsWith(code, "Alt", base::CompareCase::SENSITIVE))
    return "Alt";
  if (base::StartsWith(code, "Arrow", base::CompareCase::SENSITIVE))
    return code.substr(5);
  if (code == "Escape")
    return "Esc";
  if (code == "Backspace" || code == "Tab" || code == "Enter" ||
      code == "CapsLock" || code == "Power")
    return code;
  // Cases for media keys.
  switch (event.key_code()) {
    case ui::VKEY_BROWSER_BACK:
    case ui::VKEY_F1:
      return "HistoryBack";
    case ui::VKEY_BROWSER_FORWARD:
    case ui::VKEY_F2:
      return "HistoryForward";
    case ui::VKEY_BROWSER_REFRESH:
    case ui::VKEY_F3:
      return "BrowserRefresh";
    case ui::VKEY_MEDIA_LAUNCH_APP2:
    case ui::VKEY_F4:
      return "ChromeOSFullscreen";
    case ui::VKEY_MEDIA_LAUNCH_APP1:
    case ui::VKEY_F5:
      return "ChromeOSSwitchWindow";
    case ui::VKEY_BRIGHTNESS_DOWN:
    case ui::VKEY_F6:
      return "BrightnessDown";
    case ui::VKEY_BRIGHTNESS_UP:
    case ui::VKEY_F7:
      return "BrightnessUp";
    case ui::VKEY_VOLUME_MUTE:
    case ui::VKEY_F8:
      return "AudioVolumeMute";
    case ui::VKEY_VOLUME_DOWN:
    case ui::VKEY_F9:
      return "AudioVolumeDown";
    case ui::VKEY_VOLUME_UP:
    case ui::VKEY_F10:
      return "AudioVolumeUp";
    default:
      break;
  }
  uint16_t ch = 0;
  // Ctrl+? cases, gets key value for Ctrl is not down.
  if (event.flags() & ui::EF_CONTROL_DOWN) {
    ui::KeyEvent event_no_ctrl(event.type(), event.key_code(),
                               event.flags() ^ ui::EF_CONTROL_DOWN);
    ch = event_no_ctrl.GetCharacter();
  } else {
    ch = event.GetCharacter();
  }
  return base::UTF16ToUTF8(base::string16(1, ch));
}
#endif  // defined(OS_CHROMEOS)

void GetExtensionKeyboardEventFromKeyEvent(
    const ui::KeyEvent& event,
    InputMethodEngineBase::KeyboardEvent* ext_event) {
  DCHECK(event.type() == ui::ET_KEY_RELEASED ||
         event.type() == ui::ET_KEY_PRESSED);
  DCHECK(ext_event);
  ext_event->type = (event.type() == ui::ET_KEY_RELEASED) ? "keyup" : "keydown";

  if (event.code() == ui::DomCode::NONE) {
// TODO(azurewei): Use KeycodeConverter::DomCodeToCodeString on all platforms
#if defined(OS_CHROMEOS)
    ext_event->code = ui::KeyboardCodeToDomKeycode(event.key_code());
#else
    ext_event->code =
        std::string(ui::KeycodeConverter::DomCodeToCodeString(event.code()));
#endif
  } else {
    ext_event->code = event.GetCodeString();
  }
  ext_event->key_code = static_cast<int>(event.key_code());
  ext_event->alt_key = event.IsAltDown();
  ext_event->ctrl_key = event.IsControlDown();
  ext_event->shift_key = event.IsShiftDown();
  ext_event->caps_lock = event.IsCapsLockOn();
#if defined(OS_CHROMEOS)
  ext_event->key = GetKeyFromEvent(event);
#else
  ext_event->key = ui::KeycodeConverter::DomKeyToKeyString(event.GetDomKey());
#endif  // defined(OS_CHROMEOS)
}

}  // namespace

InputMethodEngineBase::InputMethodEngineBase()
    : current_input_type_(ui::TEXT_INPUT_TYPE_NONE),
      context_id_(0),
      next_context_id_(1),
      composition_text_(new ui::CompositionText()),
      composition_cursor_(0),
      sent_key_event_(NULL),
      profile_(NULL) {}

InputMethodEngineBase::~InputMethodEngineBase() {}

void InputMethodEngineBase::Initialize(
    scoped_ptr<ui::IMEEngineObserver> observer,
    const char* extension_id,
    Profile* profile) {
  DCHECK(observer) << "Observer must not be null.";

  // TODO(komatsu): It is probably better to set observer out of Initialize.
  observer_ = std::move(observer);
  extension_id_ = extension_id;
  profile_ = profile;
}

const std::string& InputMethodEngineBase::GetActiveComponentId() const {
  return active_component_id_;
}

bool InputMethodEngineBase::SetComposition(
    int context_id,
    const char* text,
    int selection_start,
    int selection_end,
    int cursor,
    const std::vector<SegmentInfo>& segments,
    std::string* error) {
  if (!IsActive()) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  composition_cursor_ = cursor;
  composition_text_.reset(new ui::CompositionText());
  composition_text_->text = base::UTF8ToUTF16(text);

  composition_text_->selection.set_start(selection_start);
  composition_text_->selection.set_end(selection_end);

  // TODO: Add support for displaying selected text in the composition string.
  for (std::vector<SegmentInfo>::const_iterator segment = segments.begin();
       segment != segments.end(); ++segment) {
    ui::CompositionUnderline underline;

    switch (segment->style) {
      case SEGMENT_STYLE_UNDERLINE:
        underline.color = SK_ColorBLACK;
        break;
      case SEGMENT_STYLE_DOUBLE_UNDERLINE:
        underline.color = SK_ColorBLACK;
        underline.thick = true;
        break;
      case SEGMENT_STYLE_NO_UNDERLINE:
        underline.color = SK_ColorTRANSPARENT;
        break;
      default:
        continue;
    }

    underline.start_offset = segment->start;
    underline.end_offset = segment->end;
    composition_text_->underlines.push_back(underline);
  }

  // TODO(nona): Makes focus out mode configuable, if necessary.
  UpdateComposition(*composition_text_, composition_cursor_, true);
  return true;
}

bool InputMethodEngineBase::ClearComposition(int context_id,
                                             std::string* error) {
  if (!IsActive()) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  composition_cursor_ = 0;
  composition_text_.reset(new ui::CompositionText());
  UpdateComposition(*composition_text_, composition_cursor_, false);
  return true;
}

bool InputMethodEngineBase::CommitText(int context_id,
                                       const char* text,
                                       std::string* error) {
  if (!IsActive()) {
    // TODO: Commit the text anyways.
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  ui::IMEBridge::Get()->GetInputContextHandler()->CommitText(text);

  // Records histograms for committed characters.
  if (!composition_text_->text.empty()) {
    size_t len = GetUtf8StringLength(text);
    UMA_HISTOGRAM_CUSTOM_COUNTS("InputMethod.CommitLength", len, 1, 25, 25);
    composition_text_.reset(new ui::CompositionText());
  }
  return true;
}

bool InputMethodEngineBase::DeleteSurroundingText(int context_id,
                                                  int offset,
                                                  size_t number_of_chars,
                                                  std::string* error) {
  if (!IsActive()) {
    *error = kErrorNotActive;
    return false;
  }
  if (context_id != context_id_ || context_id_ == -1) {
    *error = kErrorWrongContext;
    return false;
  }

  // TODO(nona): Return false if there is ongoing composition.

  ui::IMEInputContextHandlerInterface* input_context =
      ui::IMEBridge::Get()->GetInputContextHandler();
  if (input_context)
    input_context->DeleteSurroundingText(offset, number_of_chars);

  return true;
}

void InputMethodEngineBase::SetCompositionBounds(
    const std::vector<gfx::Rect>& bounds) {
  observer_->OnCompositionBoundsChanged(bounds);
}

void InputMethodEngineBase::FocusIn(
    const ui::IMEEngineHandlerInterface::InputContext& input_context) {
  current_input_type_ = input_context.type;

  if (!IsActive() || current_input_type_ == ui::TEXT_INPUT_TYPE_NONE)
    return;

  context_id_ = next_context_id_;
  ++next_context_id_;

  observer_->OnFocus(ui::IMEEngineHandlerInterface::InputContext(
      context_id_, input_context.type, input_context.mode,
      input_context.flags));
}

void InputMethodEngineBase::FocusOut() {
  if (!IsActive() || current_input_type_ == ui::TEXT_INPUT_TYPE_NONE)
    return;

  current_input_type_ = ui::TEXT_INPUT_TYPE_NONE;

  int context_id = context_id_;
  context_id_ = -1;
  observer_->OnBlur(context_id);
}

void InputMethodEngineBase::Enable(const std::string& component_id) {
  DCHECK(!component_id.empty());
  active_component_id_ = component_id;
  observer_->OnActivate(component_id);
  const ui::IMEEngineHandlerInterface::InputContext& input_context =
      ui::IMEBridge::Get()->GetCurrentInputContext();
  current_input_type_ = input_context.type;
  FocusIn(input_context);
}

void InputMethodEngineBase::Disable() {
  active_component_id_.clear();
  if (ui::IMEBridge::Get()->GetInputContextHandler())
    ui::IMEBridge::Get()->GetInputContextHandler()->CommitText(
        base::UTF16ToUTF8(composition_text_->text));
  composition_text_.reset(new ui::CompositionText());
  observer_->OnDeactivated(active_component_id_);
}

void InputMethodEngineBase::Reset() {
  composition_text_.reset(new ui::CompositionText());
  observer_->OnReset(active_component_id_);
}

bool InputMethodEngineBase::IsInterestedInKeyEvent() const {
  return observer_->IsInterestedInKeyEvent();
}

void InputMethodEngineBase::ProcessKeyEvent(const ui::KeyEvent& key_event,
                                            KeyEventDoneCallback& callback) {
  KeyboardEvent ext_event;
  GetExtensionKeyboardEventFromKeyEvent(key_event, &ext_event);

  // If the given key event is equal to the key event sent by
  // SendKeyEvents, this engine ID is propagated to the extension IME.
  // Note, this check relies on that ui::KeyEvent is propagated as
  // reference without copying.
  if (&key_event == sent_key_event_)
    ext_event.extension_id = extension_id_;

  observer_->OnKeyEvent(active_component_id_, ext_event, callback);
}

void InputMethodEngineBase::SetSurroundingText(const std::string& text,
                                               uint32_t cursor_pos,
                                               uint32_t anchor_pos,
                                               uint32_t offset_pos) {
  observer_->OnSurroundingTextChanged(
      active_component_id_, text, static_cast<int>(cursor_pos),
      static_cast<int>(anchor_pos), static_cast<int>(offset_pos));
}

}  // namespace input_method
