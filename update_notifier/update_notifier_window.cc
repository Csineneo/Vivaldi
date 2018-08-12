// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved

#include "update_notifier/update_notifier_window.h"

#include <Windows.h>

#include <CommCtrl.h>
#include <Shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#include "app/vivaldi_resources.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/current_module.h"
#include "ui/base/l10n/l10n_util.h"
#include "update_notifier/update_notifier_manager.h"
#include "update_notifier/update_notifier_resources.h"

namespace vivaldi_update_notifier {

namespace {
const int kNotificationUid = 1;
const int kNotificationCallbackMessage = WM_USER + 1;

const int kUpdateMenuItemId = 1;
const int kIgnoreMenuItemId = 2;
const int kQuitMenuItemId = 3;

const wchar_t kUpdateNotifierWindowClassName[] = L"VivaldiUpdateNotifierWindow";
const wchar_t kUpdateNotifierWindowName[] = L"Vivaldi Update Notifier";

void SetNotificationString(wchar_t* notification_str,
                           size_t notification_str_size,
                           const base::string16& str) {
  size_t notification_string_length =
      std::min(notification_str_size - 2, str.length());
  str.copy(notification_str, notification_string_length);
  notification_str[notification_string_length + 1] = '\0';
}
}  // anonymous namespace

class UpdateNotifierWindow::WindowClass {
 public:
  WindowClass();
  ~WindowClass();

  ATOM atom() { return atom_; }
  HINSTANCE instance() { return instance_; }

 private:
  ATOM atom_;
  HINSTANCE instance_;

  DISALLOW_COPY_AND_ASSIGN(WindowClass);
};

static base::LazyInstance<UpdateNotifierWindow::WindowClass> g_window_class =
    LAZY_INSTANCE_INITIALIZER;

UpdateNotifierWindow::WindowClass::WindowClass()
    : atom_(0), instance_(CURRENT_MODULE()) {
  WNDCLASSEX window_class;
  window_class.cbSize = sizeof(window_class);
  window_class.style = 0;
  window_class.lpfnWndProc = &UpdateNotifierWindow::WindowProc;
  window_class.cbClsExtra = 0;
  window_class.cbWndExtra = 0;
  window_class.hInstance = instance_;
  window_class.hIcon = NULL;
  window_class.hCursor = NULL;
  window_class.hbrBackground = NULL;
  window_class.lpszMenuName = NULL;
  window_class.lpszClassName = kUpdateNotifierWindowClassName;
  window_class.hIconSm = NULL;
  atom_ = RegisterClassEx(&window_class);
  if (atom_ == 0) {
    PLOG(ERROR)
        << "Failed to register the window class for a update notifier window";
  }
}

UpdateNotifierWindow::WindowClass::~WindowClass() {
  if (atom_ != 0) {
    BOOL result = UnregisterClass(MAKEINTATOM(atom_), instance_);
    DCHECK(result);
  }
}

UpdateNotifierWindow::UpdateNotifierWindow()
    : is_showing_notification_(false) {}

bool UpdateNotifierWindow::Init() {
  WindowClass& window_class = g_window_class.Get();
  hwnd_ = CreateWindowEx(WS_EX_NOACTIVATE, MAKEINTATOM(window_class.atom()),
                         kUpdateNotifierWindowName, WS_POPUP, 0, 0, 0, 0, NULL,
                         NULL, window_class.instance(), this);
  if (hwnd_ == NULL) {
    PLOG(ERROR) << "Failed to create the update notifier window";
    return false;
  }

  return notification_menu_.AppendStringMenuItem(
             l10n_util::GetStringUTF16(IDS_UPDATE_NOTIFIER_UPDATE_VIVALDI),
             MFS_DEFAULT, kUpdateMenuItemId) &&
         notification_menu_.AppendSeparator() &&
         notification_menu_.AppendStringMenuItem(
             l10n_util::GetStringUTF16(IDS_UPDATE_NOTIFIER_IGNORE_UPDATE), 0,
             kIgnoreMenuItemId) &&
         notification_menu_.AppendStringMenuItem(
             l10n_util::GetStringUTF16(IDS_UPDATE_NOTIFIER_STOP_NOTIFYING), 0,
             kQuitMenuItemId);
}

UpdateNotifierWindow::~UpdateNotifierWindow() {
  if (hwnd_ != NULL)
    DestroyWindow(hwnd_);
  RemoveNotification();
}

void UpdateNotifierWindow::ShowNotification(const std::string& version) {
  if (notification_menu_.displayed())
    return;

  NOTIFYICONDATA notify_icon = {};
  notify_icon.cbSize = sizeof(notify_icon);
  notify_icon.hWnd = hwnd_;
  notify_icon.uID = kNotificationUid;
  notify_icon.uFlags =
      NIF_MESSAGE | NIF_ICON | NIF_INFO | NIF_TIP | NIF_SHOWTIP;
  notify_icon.uCallbackMessage = kNotificationCallbackMessage;
  LoadIconMetric(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_NOTIFIER_MAIN),
                 LIM_SMALL, &notify_icon.hIcon);
  SetNotificationString(notify_icon.szTip, arraysize(notify_icon.szTip),
                        l10n_util::GetStringFUTF16(IDS_UPDATE_NOTIFICATION_TEXT,
                                                   base::UTF8ToUTF16(version)));
  notify_icon.dwInfoFlags = NIIF_USER;

  if (is_showing_notification_)
    Shell_NotifyIcon(NIM_MODIFY, &notify_icon);
  else
    Shell_NotifyIcon(NIM_ADD, &notify_icon);
  is_showing_notification_ = true;

  notify_icon.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIcon(NIM_SETVERSION, &notify_icon);

  SetNotificationString(notify_icon.szInfo, arraysize(notify_icon.szInfo),
                        l10n_util::GetStringFUTF16(IDS_UPDATE_NOTIFICATION_TEXT,
                                                   base::UTF8ToUTF16(version)));
  notify_icon.uTimeout = 30000;
  SetNotificationString(
      notify_icon.szInfoTitle, arraysize(notify_icon.szInfoTitle),
      l10n_util::GetStringUTF16(IDS_UPDATE_NOTIFICATION_TITLE));
  Shell_NotifyIcon(NIM_MODIFY, &notify_icon);
}

void UpdateNotifierWindow::RemoveNotification() {
  if (!is_showing_notification_)
    return;

  NOTIFYICONDATA notify_icon = {};
  notify_icon.cbSize = sizeof(notify_icon);
  notify_icon.hWnd = hwnd_;
  notify_icon.uID = kNotificationUid;
  Shell_NotifyIcon(NIM_DELETE, &notify_icon);
}

// static
LRESULT CALLBACK UpdateNotifierWindow::WindowProc(HWND hwnd,
                                                  UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
  UpdateNotifierWindow* self = reinterpret_cast<UpdateNotifierWindow*>(
      GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (message) {
    // Set up the self before handling WM_CREATE.
    case WM_CREATE: {
      CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
      self = reinterpret_cast<UpdateNotifierWindow*>(cs->lpCreateParams);

      // Make |hwnd| available to the message handler. At this point the control
      // hasn't returned from CreateWindow() yet.
      self->hwnd_ = hwnd;

      // Store pointer to the self to the window's user data.
      SetLastError(ERROR_SUCCESS);
      LONG_PTR result = SetWindowLongPtr(hwnd, GWLP_USERDATA,
                                         reinterpret_cast<LONG_PTR>(self));
      CHECK(result != 0 || GetLastError() == ERROR_SUCCESS);
      break;
    }

    // Clear the pointer to stop calling the self once WM_DESTROY is
    // received.
    case WM_DESTROY: {
      SetLastError(ERROR_SUCCESS);
      LONG_PTR result = SetWindowLongPtr(hwnd, GWLP_USERDATA, NULL);
      CHECK(result != 0 || GetLastError() == ERROR_SUCCESS);
      break;
    }
  }

  // Handle the message.
  if (self) {
    LRESULT message_result;
    if (self->HandleMessage(message, wparam, lparam, &message_result))
      return message_result;
  }

  return DefWindowProc(hwnd, message, wparam, lparam);
}

bool UpdateNotifierWindow::HandleMessage(UINT message,
                                         WPARAM wparam,
                                         LPARAM lparam,
                                         LRESULT* result) {
  *result = 0;
  switch (message) {
    case kNotificationCallbackMessage: {
      if (HIWORD(lparam) != kNotificationUid)
        break;

      UINT message = LOWORD(lparam);
      switch (message) {
        case WM_LBUTTONDBLCLK:
        case NIN_BALLOONUSERCLICK: {
          UpdateNotifierManager::GetInstance()->TriggerUpdate();
          RemoveNotification();
          return true;
        }

        case WM_CONTEXTMENU: {
          ShowWindow(hwnd_, SW_SHOW);
          SetForegroundWindow(hwnd_);
          notification_menu_.ShowMenu(GET_X_LPARAM(wparam),
                                      GET_Y_LPARAM(wparam), hwnd_);
          ShowWindow(hwnd_, SW_HIDE);
          return true;
        }
      }
    }

    case WM_COMMAND: {
      if (HIWORD(wparam) == 0) {
        switch (LOWORD(wparam)) {
          case kUpdateMenuItemId: {
            UpdateNotifierManager::GetInstance()->TriggerUpdate();
          }
          // Fall through
          case kIgnoreMenuItemId: {
            RemoveNotification();
            return true;
          }

          case kQuitMenuItemId: {
            if (MessageBox(NULL,
                           l10n_util::GetStringUTF16(
                               IDS_UPDATE_NOTIFIER_QUIT_MESSAGE_TEXT)
                               .c_str(),
                           l10n_util::GetStringUTF16(
                               IDS_UPDATE_NOTIFIER_QUIT_MESSAGE_TITLE)
                               .c_str(),
                           MB_OKCANCEL) == IDOK)
              UpdateNotifierManager::GetInstance()->Disable();
            return true;
          }
        }
      }
    }
  }

  return false;
}
}  // namespace vivaldi_update_notifier
