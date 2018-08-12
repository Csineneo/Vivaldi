// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/desktop_capture/desktop_capture_base.h"

#include <tuple>
#include <utility>

#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/media/combined_desktop_media_list.h"
#include "chrome/browser/media/desktop_media_list_ash.h"
#include "chrome/browser/media/desktop_streams_registry.h"
#include "chrome/browser/media/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/native_desktop_media_list.h"
#include "chrome/browser/media/tab_desktop_media_list.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "chrome/common/channel_info.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/switches.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_options.h"
#include "third_party/webrtc/modules/desktop_capture/screen_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/window_capturer.h"

namespace extensions {

namespace {

const char kInvalidSourceNameError[] = "Invalid source type specified.";
const char kEmptySourcesListError[] =
    "At least one source type must be specified.";

DesktopCaptureChooseDesktopMediaFunctionBase::PickerFactory* g_picker_factory =
    NULL;

}  // namespace

// static
void DesktopCaptureChooseDesktopMediaFunctionBase::SetPickerFactoryForTests(
    PickerFactory* factory) {
  g_picker_factory = factory;
}

DesktopCaptureChooseDesktopMediaFunctionBase::
    DesktopCaptureChooseDesktopMediaFunctionBase() {
}

DesktopCaptureChooseDesktopMediaFunctionBase::
    ~DesktopCaptureChooseDesktopMediaFunctionBase() {
  // RenderViewHost may be already destroyed.
  if (render_frame_host()) {
    DesktopCaptureRequestsRegistry::GetInstance()->RemoveRequest(
        render_frame_host()->GetProcess()->GetID(), request_id_);
  }
}

void DesktopCaptureChooseDesktopMediaFunctionBase::Cancel() {
  // Keep reference to |this| to ensure the object doesn't get destroyed before
  // we return.
  scoped_refptr<DesktopCaptureChooseDesktopMediaFunctionBase> self(this);
  if (picker_) {
    picker_.reset();
    SetResult(new base::StringValue(std::string()));
    SendResponse(true);
  }
}

bool DesktopCaptureChooseDesktopMediaFunctionBase::Execute(
    const std::vector<api::desktop_capture::DesktopCaptureSourceType>& sources,
    content::WebContents* web_contents,
    const GURL& origin,
    const base::string16 target_name) {
  // Register to be notified when the tab is closed.
  Observe(web_contents);

  bool show_screens = false;
  bool show_windows = false;
  bool show_tabs = false;
  bool request_audio = false;

  for (auto source_type : sources) {
    switch (source_type) {
      case api::desktop_capture::DESKTOP_CAPTURE_SOURCE_TYPE_NONE:
        error_ = kInvalidSourceNameError;
        return false;

      case api::desktop_capture::DESKTOP_CAPTURE_SOURCE_TYPE_SCREEN:
        show_screens = true;
        break;

      case api::desktop_capture::DESKTOP_CAPTURE_SOURCE_TYPE_WINDOW:
        show_windows = true;
        break;

      case api::desktop_capture::DESKTOP_CAPTURE_SOURCE_TYPE_TAB:
        if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                extensions::switches::kEnableTabForDesktopShare)) {
          show_tabs = true;
        } else if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            extensions::switches::kDisableTabForDesktopShare)) {
          show_tabs = false;
        } else {
          const version_info::Channel channel = chrome::GetChannel();
          if ((channel == version_info::Channel::STABLE) ||
              (channel == version_info::Channel::BETA)) {
            show_tabs = false;
          } else {
            show_tabs = true;
          }
        }
        break;

      case api::desktop_capture::DESKTOP_CAPTURE_SOURCE_TYPE_AUDIO:
        bool has_flag = base::CommandLine::ForCurrentProcess()->HasSwitch(
            extensions::switches::kDisableDesktopCaptureAudio);
        request_audio = !has_flag;
        break;
    }
  }

  if (!show_screens && !show_windows && !show_tabs) {
    error_ = kEmptySourcesListError;
    return false;
  }

  const gfx::NativeWindow parent_window =
      web_contents->GetTopLevelNativeWindow();
  std::unique_ptr<DesktopMediaList> media_list;
  if (g_picker_factory) {
    media_list = g_picker_factory->CreateModel(show_screens, show_windows,
                                               show_tabs, request_audio);
    picker_ = g_picker_factory->CreatePicker();
  } else {
    std::vector<std::unique_ptr<DesktopMediaList>> media_lists;
    // Create a screens/windows list and push it into media_lists.
    if (show_screens || show_windows) {
#if defined(USE_ASH)
      if (chrome::IsNativeWindowInAsh(parent_window)) {
        media_lists.push_back(base::WrapUnique(new DesktopMediaListAsh(
            (show_screens ? DesktopMediaListAsh::SCREENS : 0) |
            (show_windows ? DesktopMediaListAsh::WINDOWS : 0))));
      }
#endif
      if (media_lists.empty()) {
        webrtc::DesktopCaptureOptions options =
            webrtc::DesktopCaptureOptions::CreateDefault();
        options.set_disable_effects(false);
        std::unique_ptr<webrtc::ScreenCapturer> screen_capturer(
            show_screens ? webrtc::ScreenCapturer::Create(options) : NULL);
        std::unique_ptr<webrtc::WindowCapturer> window_capturer(
            show_windows ? webrtc::WindowCapturer::Create(options) : NULL);

        media_lists.push_back(base::WrapUnique(new NativeDesktopMediaList(
            std::move(screen_capturer), std::move(window_capturer))));
      }
    }

    if (show_tabs)
      media_lists.push_back(base::WrapUnique(new TabDesktopMediaList()));

    DCHECK(!media_lists.empty());

    if (media_lists.size() == 1) {
      media_list = std::move(media_lists[0]);
    } else {
      media_list.reset(new CombinedDesktopMediaList(media_lists));
    }

    // DesktopMediaPicker is implemented only for Windows, OSX and
    // Aura Linux builds.
#if defined(TOOLKIT_VIEWS) || defined(OS_MACOSX)
    picker_ = DesktopMediaPicker::Create();
#else
    error_ = "Desktop Capture API is not yet implemented for this platform.";
    return false;
#endif
  }

  DesktopMediaPicker::DoneCallback callback = base::Bind(
      &DesktopCaptureChooseDesktopMediaFunctionBase::OnPickerDialogResults,
      this);

  picker_->Show(web_contents, parent_window, parent_window,
                base::UTF8ToUTF16(extension()->name()), target_name,
                std::move(media_list), request_audio, callback);
  origin_ = origin;
  return true;
}

void DesktopCaptureChooseDesktopMediaFunctionBase::WebContentsDestroyed() {
  Cancel();
}

void DesktopCaptureChooseDesktopMediaFunctionBase::OnPickerDialogResults(
    content::DesktopMediaID source) {
  std::string result;
  if (source.type != content::DesktopMediaID::TYPE_NONE &&
      web_contents()) {
    DesktopStreamsRegistry* registry =
        MediaCaptureDevicesDispatcher::GetInstance()->
        GetDesktopStreamsRegistry();
    // TODO(miu): Once render_frame_host() is being set, we should register the
    // exact RenderFrame requesting the stream, not the main RenderFrame.  With
    // that change, also update
    // MediaCaptureDevicesDispatcher::ProcessDesktopCaptureAccessRequest().
    // http://crbug.com/304341
    content::RenderFrameHost* const main_frame = web_contents()->GetMainFrame();
    result = registry->RegisterStream(main_frame->GetProcess()->GetID(),
                                      main_frame->GetRoutingID(),
                                      origin_,
                                      source,
                                      extension()->name());
  }

  SetResult(new base::StringValue(result));
  SendResponse(true);
}

DesktopCaptureRequestsRegistry::RequestId::RequestId(int process_id,
                                                     int request_id)
    : process_id(process_id),
      request_id(request_id) {
}

bool DesktopCaptureRequestsRegistry::RequestId::operator<(
    const RequestId& other) const {
  return std::tie(process_id, request_id) <
         std::tie(other.process_id, other.request_id);
}

DesktopCaptureCancelChooseDesktopMediaFunctionBase::
    DesktopCaptureCancelChooseDesktopMediaFunctionBase() {}

DesktopCaptureCancelChooseDesktopMediaFunctionBase::
    ~DesktopCaptureCancelChooseDesktopMediaFunctionBase() {}

bool DesktopCaptureCancelChooseDesktopMediaFunctionBase::RunSync() {
  int request_id;
  EXTENSION_FUNCTION_VALIDATE(args_->GetInteger(0, &request_id));

  DesktopCaptureRequestsRegistry::GetInstance()->CancelRequest(
      render_frame_host()->GetProcess()->GetID(), request_id);
  return true;
}

DesktopCaptureRequestsRegistry::DesktopCaptureRequestsRegistry() {}
DesktopCaptureRequestsRegistry::~DesktopCaptureRequestsRegistry() {}

// static
DesktopCaptureRequestsRegistry* DesktopCaptureRequestsRegistry::GetInstance() {
  return base::Singleton<DesktopCaptureRequestsRegistry>::get();
}

void DesktopCaptureRequestsRegistry::AddRequest(
    int process_id,
    int request_id,
    DesktopCaptureChooseDesktopMediaFunctionBase* handler) {
  requests_.insert(
      RequestsMap::value_type(RequestId(process_id, request_id), handler));
}

void DesktopCaptureRequestsRegistry::RemoveRequest(int process_id,
                                                   int request_id) {
  requests_.erase(RequestId(process_id, request_id));
}

void DesktopCaptureRequestsRegistry::CancelRequest(int process_id,
                                                   int request_id) {
  RequestsMap::iterator it = requests_.find(RequestId(process_id, request_id));
  if (it != requests_.end())
    it->second->Cancel();
}


}  // namespace extensions
