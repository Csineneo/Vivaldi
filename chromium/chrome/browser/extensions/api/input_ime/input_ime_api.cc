// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/input_ime/input_ime_api.h"

#include "base/lazy_instance.h"
#include "chrome/common/extensions/api/input_ime.h"
#include "extensions/browser/extension_registry.h"

namespace input_ime = extensions::api::input_ime;
namespace KeyEventHandled = extensions::api::input_ime::KeyEventHandled;
namespace SetComposition = extensions::api::input_ime::SetComposition;
namespace CommitText = extensions::api::input_ime::CommitText;
using ui::IMEEngineHandlerInterface;
using input_method::InputMethodEngineBase;

namespace ui {

ImeObserver::ImeObserver(const std::string& extension_id, Profile* profile)
    : extension_id_(extension_id), profile_(profile) {}

void ImeObserver::OnActivate(const std::string& component_id) {
  if (extension_id_.empty() || !HasListener(input_ime::OnActivate::kEventName))
    return;

  scoped_ptr<base::ListValue> args(input_ime::OnActivate::Create(
    component_id,
    input_ime::ParseScreenType(GetCurrentScreenType())));

  DispatchEventToExtension(extensions::events::INPUT_IME_ON_ACTIVATE,
                           input_ime::OnActivate::kEventName,
                           std::move(args));
}

void ImeObserver::OnFocus(
    const IMEEngineHandlerInterface::InputContext& context) {
  if (extension_id_.empty() || !HasListener(input_ime::OnFocus::kEventName))
    return;

  input_ime::InputContext context_value;
  context_value.context_id = context.id;
  context_value.type =
      input_ime::ParseInputContextType(ConvertInputContextType(context));
  context_value.auto_correct = ConvertInputContextAutoCorrect(context);
  context_value.auto_complete = ConvertInputContextAutoComplete(context);
  context_value.spell_check = ConvertInputContextSpellCheck(context);

  scoped_ptr<base::ListValue> args(input_ime::OnFocus::Create(context_value));

  DispatchEventToExtension(extensions::events::INPUT_IME_ON_FOCUS,
                           input_ime::OnFocus::kEventName, std::move(args));
}

void ImeObserver::OnBlur(int context_id) {
  if (extension_id_.empty() || !HasListener(input_ime::OnBlur::kEventName))
    return;

  scoped_ptr<base::ListValue> args(input_ime::OnBlur::Create(context_id));

  DispatchEventToExtension(extensions::events::INPUT_IME_ON_BLUR,
                           input_ime::OnBlur::kEventName, std::move(args));
}

void ImeObserver::OnKeyEvent(
    const std::string& component_id,
    const InputMethodEngineBase::KeyboardEvent& event,
    IMEEngineHandlerInterface::KeyEventDoneCallback& key_data) {
  if (extension_id_.empty())
    return;

  // If there is no listener for the event, no need to dispatch the event to
  // extension. Instead, releases the key event for default system behavior.
  if (!ShouldForwardKeyEvent()) {
    // Continue processing the key event so that the physical keyboard can
    // still work.
    key_data.Run(false);
    return;
  }

  extensions::InputImeEventRouter* event_router =
      extensions::GetInputImeEventRouter(profile_);
  if (!event_router || !event_router->GetActiveEngine(extension_id_))
    return;
  const std::string request_id = event_router->GetActiveEngine(extension_id_)
                                     ->AddRequest(component_id, key_data);

  input_ime::KeyboardEvent key_data_value;
  key_data_value.type = input_ime::ParseKeyboardEventType(event.type);
  key_data_value.request_id = request_id;
  if (!event.extension_id.empty())
      key_data_value.extension_id.reset(new std::string(event.extension_id));
  key_data_value.key = event.key;
  key_data_value.code = event.code;
  key_data_value.alt_key.reset(new bool(event.alt_key));
  key_data_value.ctrl_key.reset(new bool(event.ctrl_key));
  key_data_value.shift_key.reset(new bool(event.shift_key));
  key_data_value.caps_lock.reset(new bool(event.caps_lock));

  scoped_ptr<base::ListValue> args(
      input_ime::OnKeyEvent::Create(component_id, key_data_value));

  DispatchEventToExtension(extensions::events::INPUT_IME_ON_KEY_EVENT,
                           input_ime::OnKeyEvent::kEventName, std::move(args));
}

void ImeObserver::OnReset(const std::string& component_id) {
  if (extension_id_.empty() || !HasListener(input_ime::OnReset::kEventName))
    return;

  scoped_ptr<base::ListValue> args(input_ime::OnReset::Create(component_id));

  DispatchEventToExtension(extensions::events::INPUT_IME_ON_RESET,
                           input_ime::OnReset::kEventName, std::move(args));
}

void ImeObserver::OnDeactivated(const std::string& component_id) {
  if (extension_id_.empty() ||
      !HasListener(input_ime::OnDeactivated::kEventName))
    return;

  scoped_ptr<base::ListValue> args(
      input_ime::OnDeactivated::Create(component_id));

  DispatchEventToExtension(extensions::events::INPUT_IME_ON_DEACTIVATED,
                           input_ime::OnDeactivated::kEventName,
                           std::move(args));
}

// TODO(azurewei): This function implementation should be shared on all
// platforms, while with some changing on the current code on ChromeOS.
void ImeObserver::OnCompositionBoundsChanged(
    const std::vector<gfx::Rect>& bounds) {}

bool ImeObserver::IsInterestedInKeyEvent() const {
  return ShouldForwardKeyEvent();
}

void ImeObserver::OnSurroundingTextChanged(const std::string& component_id,
                                           const std::string& text,
                                           int cursor_pos,
                                           int anchor_pos,
                                           int offset_pos) {
  if (extension_id_.empty() ||
      !HasListener(input_ime::OnSurroundingTextChanged::kEventName))
    return;

  input_ime::OnSurroundingTextChanged::SurroundingInfo info;
  info.text = text;
  info.focus = cursor_pos;
  info.anchor = anchor_pos;
  info.offset = offset_pos;
  scoped_ptr<base::ListValue> args(
    input_ime::OnSurroundingTextChanged::Create(component_id, info));

  DispatchEventToExtension(
    extensions::events::INPUT_IME_ON_SURROUNDING_TEXT_CHANGED,
    input_ime::OnSurroundingTextChanged::kEventName, std::move(args));
}

bool ImeObserver::ShouldForwardKeyEvent() const {
  // Only forward key events to extension if there are non-lazy listeners
  // for onKeyEvent. Because if something wrong with the lazy background
  // page which doesn't register listener for onKeyEvent, it will not handle
  // the key events, and therefore, all key events will be eaten.
  // This is for error-tolerance, and it means that onKeyEvent will never wake
  // up lazy background page.
  const extensions::EventListenerMap::ListenerList& listener_list =
      extensions::EventRouter::Get(profile_)
          ->listeners()
          .GetEventListenersByName(input_ime::OnKeyEvent::kEventName);
  for (extensions::EventListenerMap::ListenerList::const_iterator it =
           listener_list.begin();
       it != listener_list.end(); ++it) {
    if ((*it)->extension_id() == extension_id_ && !(*it)->IsLazy())
      return true;
  }
  return false;
}

bool ImeObserver::HasListener(const std::string& event_name) const {
  return extensions::EventRouter::Get(profile_)->HasEventListener(event_name);
}

std::string ImeObserver::ConvertInputContextType(
    ui::IMEEngineHandlerInterface::InputContext input_context) {
  std::string input_context_type = "text";
  switch (input_context.type) {
    case ui::TEXT_INPUT_TYPE_SEARCH:
      input_context_type = "search";
      break;
    case ui::TEXT_INPUT_TYPE_TELEPHONE:
      input_context_type = "tel";
      break;
    case ui::TEXT_INPUT_TYPE_URL:
      input_context_type = "url";
      break;
    case ui::TEXT_INPUT_TYPE_EMAIL:
      input_context_type = "email";
      break;
    case ui::TEXT_INPUT_TYPE_NUMBER:
      input_context_type = "number";
      break;
    case ui::TEXT_INPUT_TYPE_PASSWORD:
      input_context_type = "password";
      break;
    default:
      input_context_type = "text";
      break;
  }
  return input_context_type;
}

bool ImeObserver::ConvertInputContextAutoCorrect(
    ui::IMEEngineHandlerInterface::InputContext input_context) {
  return !(input_context.flags & ui::TEXT_INPUT_FLAG_AUTOCORRECT_OFF);
}

bool ImeObserver::ConvertInputContextAutoComplete(
    ui::IMEEngineHandlerInterface::InputContext input_context) {
  return !(input_context.flags & ui::TEXT_INPUT_FLAG_AUTOCOMPLETE_OFF);
}

bool ImeObserver::ConvertInputContextSpellCheck(
    ui::IMEEngineHandlerInterface::InputContext input_context) {
  return !(input_context.flags & ui::TEXT_INPUT_FLAG_SPELLCHECK_OFF);
}

}  // namespace ui

namespace extensions {

InputImeEventRouterFactory* InputImeEventRouterFactory::GetInstance() {
  return base::Singleton<InputImeEventRouterFactory>::get();
}

InputImeEventRouterFactory::InputImeEventRouterFactory() {
}

InputImeEventRouterFactory::~InputImeEventRouterFactory() {
}

InputImeEventRouter* InputImeEventRouterFactory::GetRouter(Profile* profile) {
  if (!profile)
    return nullptr;
  InputImeEventRouter* router = router_map_[profile];
  if (!router) {
    router = new InputImeEventRouter(profile);
    router_map_[profile] = router;
  }
  return router;
}

ExtensionFunction::ResponseAction InputImeKeyEventHandledFunction::Run() {
  scoped_ptr<KeyEventHandled::Params> params(
      KeyEventHandled::Params::Create(*args_));
  InputImeEventRouter* event_router =
      GetInputImeEventRouter(Profile::FromBrowserContext(browser_context()));
  InputMethodEngineBase* engine =
      event_router ? event_router->GetActiveEngine(extension_id()) : nullptr;
  if (engine) {
    engine->KeyEventHandled(extension_id(), params->request_id,
                            params->response);
  }
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction InputImeSetCompositionFunction::Run() {
  bool success = false;
  InputImeEventRouter* event_router =
      GetInputImeEventRouter(Profile::FromBrowserContext(browser_context()));
  InputMethodEngineBase* engine =
      event_router ? event_router->GetActiveEngine(extension_id()) : nullptr;
  if (engine) {
    scoped_ptr<SetComposition::Params> parent_params(
        SetComposition::Params::Create(*args_));
    const SetComposition::Params::Parameters& params =
        parent_params->parameters;
    std::vector<InputMethodEngineBase::SegmentInfo> segments;
    if (params.segments) {
      const std::vector<
          linked_ptr<SetComposition::Params::Parameters::SegmentsType>>&
          segments_args = *params.segments;
      for (const auto& segments_arg : segments_args) {
        EXTENSION_FUNCTION_VALIDATE(segments_arg->style !=
                                    input_ime::UNDERLINE_STYLE_NONE);
        InputMethodEngineBase::SegmentInfo segment_info;
        segment_info.start = segments_arg->start;
        segment_info.end = segments_arg->end;
        if (segments_arg->style == input_ime::UNDERLINE_STYLE_UNDERLINE) {
          segment_info.style = InputMethodEngineBase::SEGMENT_STYLE_UNDERLINE;
        } else if (segments_arg->style ==
                   input_ime::UNDERLINE_STYLE_DOUBLEUNDERLINE) {
          segment_info.style =
              InputMethodEngineBase::SEGMENT_STYLE_DOUBLE_UNDERLINE;
        } else {
          segment_info.style =
              InputMethodEngineBase::SEGMENT_STYLE_NO_UNDERLINE;
        }
        segments.push_back(segment_info);
      }
    }
    int selection_start =
        params.selection_start ? *params.selection_start : params.cursor;
    int selection_end =
        params.selection_end ? *params.selection_end : params.cursor;
    success = engine->SetComposition(params.context_id, params.text.c_str(),
                                     selection_start, selection_end,
                                     params.cursor, segments, &error_);
  }
  scoped_ptr<base::ListValue> output = SetComposition::Results::Create(success);
  return RespondNow(ArgumentList(std::move(output)));
}

ExtensionFunction::ResponseAction InputImeCommitTextFunction::Run() {
  bool success = false;
  InputImeEventRouter* event_router =
      GetInputImeEventRouter(Profile::FromBrowserContext(browser_context()));
  InputMethodEngineBase* engine =
      event_router ? event_router->GetActiveEngine(extension_id()) : nullptr;
  if (engine) {
    scoped_ptr<CommitText::Params> parent_params(
        CommitText::Params::Create(*args_));
    const CommitText::Params::Parameters& params = parent_params->parameters;
    success =
        engine->CommitText(params.context_id, params.text.c_str(), &error_);
  }
  scoped_ptr<base::ListValue> output = CommitText::Results::Create(success);
  return RespondNow(ArgumentList(std::move(output)));
}

InputImeAPI::InputImeAPI(content::BrowserContext* context)
    : browser_context_(context), extension_registry_observer_(this) {
  extension_registry_observer_.Add(ExtensionRegistry::Get(browser_context_));

  EventRouter* event_router = EventRouter::Get(browser_context_);
  event_router->RegisterObserver(this, input_ime::OnFocus::kEventName);
}

InputImeAPI::~InputImeAPI() {
  EventRouter::Get(browser_context_)->UnregisterObserver(this);
}

static base::LazyInstance<BrowserContextKeyedAPIFactory<InputImeAPI> >
    g_factory = LAZY_INSTANCE_INITIALIZER;

// static
BrowserContextKeyedAPIFactory<InputImeAPI>* InputImeAPI::GetFactoryInstance() {
  return g_factory.Pointer();
}

InputImeEventRouter* GetInputImeEventRouter(Profile* profile) {
  if (!profile)
    return nullptr;
  if (profile->HasOffTheRecordProfile())
    profile = profile->GetOffTheRecordProfile();
  return extensions::InputImeEventRouterFactory::GetInstance()->GetRouter(
      profile);
}

}  // namespace extensions
