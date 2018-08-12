// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ID Not In Map Note:
// A service, characteristic, or descriptor ID not in the corresponding
// BluetoothDispatcherHost map [service_to_device_, characteristic_to_service_,
// descriptor_to_characteristic_] implies a hostile renderer because a renderer
// obtains the corresponding ID from this class and it will be added to the map
// at that time.

#include "content/browser/bluetooth/bluetooth_dispatcher_host.h"

#include <stddef.h>

#include <utility>

#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/browser/bluetooth/bluetooth_blacklist.h"
#include "content/browser/bluetooth/bluetooth_metrics.h"
#include "content/browser/bluetooth/cache_query_result.h"
#include "content/browser/bluetooth/first_device_bluetooth_chooser.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_discovery_session.h"

using blink::WebBluetoothError;
using device::BluetoothAdapter;
using device::BluetoothAdapterFactory;
using device::BluetoothUUID;

namespace content {

namespace {

// TODO(ortuno): Once we have a chooser for scanning, a way to control that
// chooser from tests, and the right callback for discovered services we should
// delete these constants.
// https://crbug.com/436280 and https://crbug.com/484504
const int kDelayTime = 5;         // 5 seconds for scanning and discovering
const int kTestingDelayTime = 0;  // No need to wait during tests
const size_t kMaxLengthForDeviceName =
    29;  // max length of device name in filter.

bool IsEmptyOrInvalidFilter(const content::BluetoothScanFilter& filter) {
  return filter.name.empty() && filter.namePrefix.empty() &&
         filter.services.empty() &&
         filter.name.length() > kMaxLengthForDeviceName &&
         filter.namePrefix.length() > kMaxLengthForDeviceName;
}

bool HasEmptyOrInvalidFilter(
    const std::vector<content::BluetoothScanFilter>& filters) {
  return filters.empty()
             ? true
             : filters.end() != std::find_if(filters.begin(), filters.end(),
                                             IsEmptyOrInvalidFilter);
}

// Defined at
// https://webbluetoothchrome.github.io/web-bluetooth/#dfn-matches-a-filter
bool MatchesFilter(const device::BluetoothDevice& device,
                   const content::BluetoothScanFilter& filter) {
  DCHECK(!IsEmptyOrInvalidFilter(filter));

  const std::string device_name = base::UTF16ToUTF8(device.GetName());

  if (!filter.name.empty() && (device_name != filter.name)) {
      return false;
  }

  if (!filter.namePrefix.empty() &&
      (!base::StartsWith(device_name, filter.namePrefix,
                         base::CompareCase::SENSITIVE))) {
    return false;
  }

  if (!filter.services.empty()) {
    const auto& device_uuid_list = device.GetUUIDs();
    const std::set<BluetoothUUID> device_uuids(device_uuid_list.begin(),
                                               device_uuid_list.end());
    for (const auto& service : filter.services) {
      if (!ContainsKey(device_uuids, service)) {
        return false;
      }
    }
  }

  return true;
}

bool MatchesFilters(const device::BluetoothDevice& device,
                    const std::vector<content::BluetoothScanFilter>& filters) {
  DCHECK(!HasEmptyOrInvalidFilter(filters));
  for (const content::BluetoothScanFilter& filter : filters) {
    if (MatchesFilter(device, filter)) {
      return true;
    }
  }
  return false;
}

void StopDiscoverySession(
    std::unique_ptr<device::BluetoothDiscoverySession> discovery_session) {
  // Nothing goes wrong if the discovery session fails to stop, and we don't
  // need to wait for it before letting the user's script proceed, so we ignore
  // the results here.
  discovery_session->Stop(base::Bind(&base::DoNothing),
                          base::Bind(&base::DoNothing));
}

UMARequestDeviceOutcome OutcomeFromChooserEvent(BluetoothChooser::Event event) {
  switch (event) {
    case BluetoothChooser::Event::DENIED_PERMISSION:
      return UMARequestDeviceOutcome::BLUETOOTH_CHOOSER_DENIED_PERMISSION;
    case BluetoothChooser::Event::CANCELLED:
      return UMARequestDeviceOutcome::BLUETOOTH_CHOOSER_CANCELLED;
    case BluetoothChooser::Event::SHOW_OVERVIEW_HELP:
      return UMARequestDeviceOutcome::BLUETOOTH_OVERVIEW_HELP_LINK_PRESSED;
    case BluetoothChooser::Event::SHOW_ADAPTER_OFF_HELP:
      return UMARequestDeviceOutcome::ADAPTER_OFF_HELP_LINK_PRESSED;
    case BluetoothChooser::Event::SHOW_NEED_LOCATION_HELP:
      return UMARequestDeviceOutcome::NEED_LOCATION_HELP_LINK_PRESSED;
    case BluetoothChooser::Event::SELECTED:
      // We can't know if we are going to send a success message yet because
      // the device could have vanished. This event should be histogramed
      // manually after checking if the device is still around.
      NOTREACHED();
      return UMARequestDeviceOutcome::SUCCESS;
    case BluetoothChooser::Event::RESCAN:
      // Rescanning doesn't result in a IPC message for the request being sent
      // so no need to histogram it.
      NOTREACHED();
      return UMARequestDeviceOutcome::SUCCESS;
  }
  NOTREACHED();
  return UMARequestDeviceOutcome::SUCCESS;
}

}  //  namespace

BluetoothDispatcherHost::BluetoothDispatcherHost(int render_process_id)
    : BrowserMessageFilter(BluetoothMsgStart),
      render_process_id_(render_process_id),
      current_delay_time_(kDelayTime),
      discovery_session_timer_(
          FROM_HERE,
          // TODO(jyasskin): Add a way for tests to control the dialog
          // directly, and change this to a reasonable discovery timeout.
          base::TimeDelta::FromSecondsD(current_delay_time_),
          base::Bind(&BluetoothDispatcherHost::StopDeviceDiscovery,
                     // base::Timer guarantees it won't call back after its
                     // destructor starts.
                     base::Unretained(this)),
          /*is_repeating=*/false),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Bind all future weak pointers to the UI thread.
  weak_ptr_on_ui_thread_ = weak_ptr_factory_.GetWeakPtr();
  weak_ptr_on_ui_thread_.get();  // Associates with UI thread.
}

void BluetoothDispatcherHost::OnDestruct() const {
  // See class comment: UI Thread Note.
  BrowserThread::DeleteOnUIThread::Destruct(this);
}

void BluetoothDispatcherHost::OverrideThreadForMessage(
    const IPC::Message& message,
    content::BrowserThread::ID* thread) {
  // See class comment: UI Thread Note.
  *thread = BrowserThread::UI;
}

bool BluetoothDispatcherHost::OnMessageReceived(const IPC::Message& message) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BluetoothDispatcherHost, message)
  IPC_MESSAGE_HANDLER(BluetoothHostMsg_RequestDevice, OnRequestDevice)
  IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void BluetoothDispatcherHost::SetBluetoothAdapterForTesting(
    scoped_refptr<device::BluetoothAdapter> mock_adapter) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (mock_adapter.get()) {
    current_delay_time_ = kTestingDelayTime;
    // Reset the discovery session timer to use the new delay time.
    discovery_session_timer_.Start(
        FROM_HERE, base::TimeDelta::FromSecondsD(current_delay_time_),
        base::Bind(&BluetoothDispatcherHost::StopDeviceDiscovery,
                   // base::Timer guarantees it won't call back after its
                   // destructor starts.
                   base::Unretained(this)));
  } else {
    // The following data structures are used to store pending operations.
    // They should never contain elements at the end of a test.
    DCHECK(request_device_sessions_.IsEmpty());

    // The following data structures are cleaned up when a
    // device/service/characteristic is removed.
    // Since this can happen after the test is done and the cleanup function is
    // called, we clean them here.
    allowed_devices_map_ = BluetoothAllowedDevicesMap();
  }

  set_adapter(std::move(mock_adapter));
}

BluetoothDispatcherHost::~BluetoothDispatcherHost() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Clear adapter, releasing observer references.
  set_adapter(scoped_refptr<device::BluetoothAdapter>());
}

// Stores information associated with an in-progress requestDevice call. This
// will include the state of the active chooser dialog in a future patch.
struct BluetoothDispatcherHost::RequestDeviceSession {
 public:
  RequestDeviceSession(int thread_id,
                       int request_id,
                       url::Origin origin,
                       const std::vector<BluetoothScanFilter>& filters,
                       const std::vector<BluetoothUUID>& optional_services)
      : thread_id(thread_id),
        request_id(request_id),
        origin(origin),
        filters(filters),
        optional_services(optional_services) {}

  void AddFilteredDevice(const device::BluetoothDevice& device) {
    if (chooser && MatchesFilters(device, filters)) {
      chooser->AddDevice(device.GetAddress(), device.GetName());
    }
  }

  std::unique_ptr<device::BluetoothDiscoveryFilter> ComputeScanFilter() const {
    std::set<BluetoothUUID> services;
    for (const BluetoothScanFilter& filter : filters) {
      services.insert(filter.services.begin(), filter.services.end());
    }
    std::unique_ptr<device::BluetoothDiscoveryFilter> discovery_filter(
        new device::BluetoothDiscoveryFilter(
            device::BluetoothDiscoveryFilter::TRANSPORT_DUAL));
    for (const BluetoothUUID& service : services) {
      discovery_filter->AddUUID(service);
    }
    return discovery_filter;
  }

  const int thread_id;
  const int request_id;
  const url::Origin origin;
  const std::vector<BluetoothScanFilter> filters;
  const std::vector<BluetoothUUID> optional_services;
  std::unique_ptr<BluetoothChooser> chooser;
  std::unique_ptr<device::BluetoothDiscoverySession> discovery_session;
};

void BluetoothDispatcherHost::set_adapter(
    scoped_refptr<device::BluetoothAdapter> adapter) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (adapter_.get()) {
    adapter_->RemoveObserver(this);
    for (device::BluetoothAdapter::Observer* observer : adapter_observers_) {
      adapter_->RemoveObserver(observer);
    }
  }
  adapter_ = adapter;
  if (adapter_.get()) {
    adapter_->AddObserver(this);
    for (device::BluetoothAdapter::Observer* observer : adapter_observers_) {
      adapter_->AddObserver(observer);
    }
  } else {
    // Notify that the adapter has been removed and observers should clean up
    // their state.
    for (device::BluetoothAdapter::Observer* observer : adapter_observers_) {
      observer->AdapterPresentChanged(nullptr, false);
    }
  }
}

void BluetoothDispatcherHost::StartDeviceDiscovery(
    RequestDeviceSession* session,
    int chooser_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (session->discovery_session) {
    // Already running; just increase the timeout.
    discovery_session_timer_.Reset();
  } else {
    session->chooser->ShowDiscoveryState(
        BluetoothChooser::DiscoveryState::DISCOVERING);
    adapter_->StartDiscoverySessionWithFilter(
        session->ComputeScanFilter(),
        base::Bind(&BluetoothDispatcherHost::OnDiscoverySessionStarted,
                   weak_ptr_on_ui_thread_, chooser_id),
        base::Bind(&BluetoothDispatcherHost::OnDiscoverySessionStartedError,
                   weak_ptr_on_ui_thread_, chooser_id));
  }
}

void BluetoothDispatcherHost::StopDeviceDiscovery() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    if (session->discovery_session) {
      StopDiscoverySession(std::move(session->discovery_session));
    }
    if (session->chooser) {
      session->chooser->ShowDiscoveryState(
          BluetoothChooser::DiscoveryState::IDLE);
    }
  }
}

void BluetoothDispatcherHost::AdapterPoweredChanged(
    device::BluetoothAdapter* adapter,
    bool powered) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  const BluetoothChooser::AdapterPresence presence =
      powered ? BluetoothChooser::AdapterPresence::POWERED_ON
              : BluetoothChooser::AdapterPresence::POWERED_OFF;
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();

    // Stop ongoing discovery session if power is off.
    if (!powered && session->discovery_session) {
      StopDiscoverySession(std::move(session->discovery_session));
    }

    if (session->chooser)
      session->chooser->SetAdapterPresence(presence);
  }

  // Stop the timer so that we don't change the state of the chooser
  // when timer expires.
  if (!powered) {
    discovery_session_timer_.Stop();
  }
}

void BluetoothDispatcherHost::DeviceAdded(device::BluetoothAdapter* adapter,
                                          device::BluetoothDevice* device) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Adding device to all choosers: " << device->GetAddress();
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    session->AddFilteredDevice(*device);
  }
}

void BluetoothDispatcherHost::DeviceRemoved(device::BluetoothAdapter* adapter,
                                            device::BluetoothDevice* device) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Marking device removed on all choosers: " << device->GetAddress();
  for (IDMap<RequestDeviceSession, IDMapOwnPointer>::iterator iter(
           &request_device_sessions_);
       !iter.IsAtEnd(); iter.Advance()) {
    RequestDeviceSession* session = iter.GetCurrentValue();
    if (session->chooser) {
      session->chooser->RemoveDevice(device->GetAddress());
    }
  }
}

void BluetoothDispatcherHost::OnRequestDevice(
    int thread_id,
    int request_id,
    int frame_routing_id,
    const std::vector<BluetoothScanFilter>& filters,
    const std::vector<BluetoothUUID>& optional_services) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RecordWebBluetoothFunctionCall(UMAWebBluetoothFunction::REQUEST_DEVICE);
  RecordRequestDeviceArguments(filters, optional_services);

  if (!adapter_.get()) {
    if (BluetoothAdapterFactory::IsBluetoothAdapterAvailable()) {
      BluetoothAdapterFactory::GetAdapter(base::Bind(
          &BluetoothDispatcherHost::OnGetAdapter, weak_ptr_on_ui_thread_,
          base::Bind(&BluetoothDispatcherHost::OnRequestDeviceImpl,
                     weak_ptr_on_ui_thread_, thread_id, request_id,
                     frame_routing_id, filters, optional_services)));
      return;
    }
    RecordRequestDeviceOutcome(UMARequestDeviceOutcome::NO_BLUETOOTH_ADAPTER);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id, WebBluetoothError::NO_BLUETOOTH_ADAPTER));
    return;
  }
  OnRequestDeviceImpl(thread_id, request_id, frame_routing_id, filters,
                      optional_services);
}

void BluetoothDispatcherHost::OnGetAdapter(
    base::Closure continuation,
    scoped_refptr<device::BluetoothAdapter> adapter) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  set_adapter(adapter);
  continuation.Run();
}

void BluetoothDispatcherHost::OnRequestDeviceImpl(
    int thread_id,
    int request_id,
    int frame_routing_id,
    const std::vector<BluetoothScanFilter>& filters,
    const std::vector<BluetoothUUID>& optional_services) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  VLOG(1) << "requestDevice called with the following filters: ";
  for (const BluetoothScanFilter& filter : filters) {
    VLOG(1) << "Name: " << filter.name;
    VLOG(1) << "Name Prefix: " << filter.namePrefix;
    VLOG(1) << "Services:";
    VLOG(1) << "\t[";
    for (const BluetoothUUID& service : filter.services)
      VLOG(1) << "\t\t" << service.value();
    VLOG(1) << "\t]";
  }

  VLOG(1) << "requestDevice called with the following optional services: ";
  for (const BluetoothUUID& service : optional_services)
    VLOG(1) << "\t" << service.value();

  // Check blacklist to reject invalid filters and adjust optional_services.
  if (BluetoothBlacklist::Get().IsExcluded(filters)) {
    RecordRequestDeviceOutcome(
        UMARequestDeviceOutcome::BLACKLISTED_SERVICE_IN_FILTER);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id,
        WebBluetoothError::REQUEST_DEVICE_WITH_BLACKLISTED_UUID));
    return;
  }
  std::vector<BluetoothUUID> optional_services_blacklist_filtered(
      optional_services);
  BluetoothBlacklist::Get().RemoveExcludedUuids(
      &optional_services_blacklist_filtered);

  RenderFrameHostImpl* render_frame_host =
      RenderFrameHostImpl::FromID(render_process_id_, frame_routing_id);
  WebContents* web_contents =
      WebContents::FromRenderFrameHost(render_frame_host);

  if (!render_frame_host || !web_contents) {
    DLOG(WARNING) << "Got a requestDevice IPC without a matching "
                  << "RenderFrameHost or WebContents: " << render_process_id_
                  << ", " << frame_routing_id;
    RecordRequestDeviceOutcome(UMARequestDeviceOutcome::NO_RENDER_FRAME);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id,
        WebBluetoothError::REQUEST_DEVICE_WITHOUT_FRAME));
    return;
  }

  const url::Origin requesting_origin =
      render_frame_host->GetLastCommittedOrigin();
  const url::Origin embedding_origin =
      web_contents->GetMainFrame()->GetLastCommittedOrigin();

  // TODO(crbug.com/518042): Enforce correctly-delegated permissions instead of
  // matching origins. When relaxing this, take care to handle non-sandboxed
  // unique origins.
  if (!embedding_origin.IsSameOriginWith(requesting_origin)) {
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id,
        WebBluetoothError::REQUEST_DEVICE_FROM_CROSS_ORIGIN_IFRAME));
    return;
  }
  // The above also excludes unique origins, which are not even same-origin with
  // themselves.
  DCHECK(!requesting_origin.unique());

  DCHECK(adapter_.get());

  if (!adapter_->IsPresent()) {
    VLOG(1) << "Bluetooth Adapter not present. Can't serve requestDevice.";
    RecordRequestDeviceOutcome(
        UMARequestDeviceOutcome::BLUETOOTH_ADAPTER_NOT_PRESENT);
    Send(new BluetoothMsg_RequestDeviceError(
        thread_id, request_id, WebBluetoothError::NO_BLUETOOTH_ADAPTER));
    return;
  }

  // The renderer should never send empty filters.
  if (HasEmptyOrInvalidFilter(filters)) {
    bad_message::ReceivedBadMessage(this,
                                    bad_message::BDH_EMPTY_OR_INVALID_FILTERS);
    return;
  }

  switch (GetContentClient()->browser()->AllowWebBluetooth(
      web_contents->GetBrowserContext(), requesting_origin, embedding_origin)) {
    case ContentBrowserClient::AllowWebBluetoothResult::BLOCK_POLICY: {
      RecordRequestDeviceOutcome(
          UMARequestDeviceOutcome::BLUETOOTH_CHOOSER_POLICY_DISABLED);
      Send(new BluetoothMsg_RequestDeviceError(
          thread_id, request_id,
          WebBluetoothError::CHOOSER_NOT_SHOWN_API_LOCALLY_DISABLED));
      return;
    }
    case ContentBrowserClient::AllowWebBluetoothResult::
        BLOCK_GLOBALLY_DISABLED: {
      // Log to the developer console.
      web_contents->GetMainFrame()->AddMessageToConsole(
          content::CONSOLE_MESSAGE_LEVEL_LOG,
          "Bluetooth permission has been blocked.");
      // Block requests.
      RecordRequestDeviceOutcome(
          UMARequestDeviceOutcome::BLUETOOTH_GLOBALLY_DISABLED);
      Send(new BluetoothMsg_RequestDeviceError(
          thread_id, request_id,
          WebBluetoothError::CHOOSER_NOT_SHOWN_API_GLOBALLY_DISABLED));
      return;
    }
    case ContentBrowserClient::AllowWebBluetoothResult::ALLOW:
      break;
  }

  // Create storage for the information that backs the chooser, and show the
  // chooser.
  RequestDeviceSession* const session =
      new RequestDeviceSession(thread_id, request_id, requesting_origin,
                               filters, optional_services_blacklist_filtered);
  int chooser_id = request_device_sessions_.Add(session);

  BluetoothChooser::EventHandler chooser_event_handler =
      base::Bind(&BluetoothDispatcherHost::OnBluetoothChooserEvent,
                 weak_ptr_on_ui_thread_, chooser_id);
  if (WebContentsDelegate* delegate = web_contents->GetDelegate()) {
    session->chooser =
        delegate->RunBluetoothChooser(render_frame_host, chooser_event_handler);
  }
  if (!session->chooser) {
    LOG(WARNING)
        << "No Bluetooth chooser implementation; falling back to first device.";
    session->chooser.reset(
        new FirstDeviceBluetoothChooser(chooser_event_handler));
  }

  if (!session->chooser->CanAskForScanningPermission()) {
    VLOG(1) << "Closing immediately because Chooser cannot obtain permission.";
    OnBluetoothChooserEvent(chooser_id,
                            BluetoothChooser::Event::DENIED_PERMISSION, "");
    return;
  }

  // Populate the initial list of devices.
  VLOG(1) << "Populating " << adapter_->GetDevices().size()
          << " devices in chooser " << chooser_id;
  for (const device::BluetoothDevice* device : adapter_->GetDevices()) {
    VLOG(1) << "\t" << device->GetAddress();
    session->AddFilteredDevice(*device);
  }

  if (!session->chooser) {
    // If the dialog's closing, no need to do any of the rest of this.
    return;
  }

  if (!adapter_->IsPowered()) {
    session->chooser->SetAdapterPresence(
        BluetoothChooser::AdapterPresence::POWERED_OFF);
    return;
  }

  StartDeviceDiscovery(session, chooser_id);
}

void BluetoothDispatcherHost::OnDiscoverySessionStarted(
    int chooser_id,
    std::unique_ptr<device::BluetoothDiscoverySession> discovery_session) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Started discovery session for " << chooser_id;
  if (RequestDeviceSession* session =
          request_device_sessions_.Lookup(chooser_id)) {
    session->discovery_session = std::move(discovery_session);

    // Arrange to stop discovery later.
    discovery_session_timer_.Reset();
  } else {
    VLOG(1) << "Chooser " << chooser_id
            << " was closed before the session finished starting. Stopping.";
    StopDiscoverySession(std::move(discovery_session));
  }
}

void BluetoothDispatcherHost::OnDiscoverySessionStartedError(int chooser_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  VLOG(1) << "Failed to start discovery session for " << chooser_id;
  if (RequestDeviceSession* session =
          request_device_sessions_.Lookup(chooser_id)) {
    if (session->chooser && !session->discovery_session) {
      session->chooser->ShowDiscoveryState(
          BluetoothChooser::DiscoveryState::FAILED_TO_START);
    }
  }
  // Ignore discovery session start errors when the dialog was already closed by
  // the time they happen.
}

void BluetoothDispatcherHost::OnBluetoothChooserEvent(
    int chooser_id,
    BluetoothChooser::Event event,
    const std::string& device_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RequestDeviceSession* session = request_device_sessions_.Lookup(chooser_id);
  DCHECK(session) << "Shouldn't receive an event (" << static_cast<int>(event)
                  << ") from a closed chooser.";
  CHECK(session->chooser) << "Shouldn't receive an event ("
                          << static_cast<int>(event)
                          << ") from a closed chooser.";
  switch (event) {
    case BluetoothChooser::Event::RESCAN:
      StartDeviceDiscovery(session, chooser_id);
      // No need to close the chooser so we return.
      return;
    case BluetoothChooser::Event::DENIED_PERMISSION:
    case BluetoothChooser::Event::CANCELLED:
    case BluetoothChooser::Event::SELECTED:
      break;
    case BluetoothChooser::Event::SHOW_OVERVIEW_HELP:
      VLOG(1) << "Overview Help link pressed.";
      break;
    case BluetoothChooser::Event::SHOW_ADAPTER_OFF_HELP:
      VLOG(1) << "Adapter Off Help link pressed.";
      break;
    case BluetoothChooser::Event::SHOW_NEED_LOCATION_HELP:
      VLOG(1) << "Need Location Help link pressed.";
      break;
  }

  // Synchronously ensure nothing else calls into the chooser after it has
  // asked to be closed.
  session->chooser.reset();

  // Yield to the event loop to make sure we don't destroy the session
  // within a BluetoothDispatcherHost stack frame.
  if (!base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&BluetoothDispatcherHost::FinishClosingChooser,
                     weak_ptr_on_ui_thread_, chooser_id, event, device_id))) {
    LOG(WARNING) << "No TaskRunner; not closing requestDevice dialog.";
  }
}

void BluetoothDispatcherHost::FinishClosingChooser(
    int chooser_id,
    BluetoothChooser::Event event,
    const std::string& device_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RequestDeviceSession* session = request_device_sessions_.Lookup(chooser_id);
  DCHECK(session) << "Session removed unexpectedly.";

  if ((event != BluetoothChooser::Event::DENIED_PERMISSION) &&
      (event != BluetoothChooser::Event::SELECTED)) {
    RecordRequestDeviceOutcome(OutcomeFromChooserEvent(event));
    Send(new BluetoothMsg_RequestDeviceError(
        session->thread_id, session->request_id,
        WebBluetoothError::CHOOSER_CANCELLED));
    request_device_sessions_.Remove(chooser_id);
    return;
  }
  if (event == BluetoothChooser::Event::DENIED_PERMISSION) {
    RecordRequestDeviceOutcome(
        UMARequestDeviceOutcome::BLUETOOTH_CHOOSER_DENIED_PERMISSION);
    VLOG(1) << "Bluetooth chooser denied permission";
    Send(new BluetoothMsg_RequestDeviceError(
        session->thread_id, session->request_id,
        WebBluetoothError::CHOOSER_NOT_SHOWN_USER_DENIED_PERMISSION_TO_SCAN));
    request_device_sessions_.Remove(chooser_id);
    return;
  }
  DCHECK_EQ(static_cast<int>(event),
            static_cast<int>(BluetoothChooser::Event::SELECTED));

  // |device_id| is the Device Address that RequestDeviceSession passed to
  // chooser->AddDevice().
  const device::BluetoothDevice* const device = adapter_->GetDevice(device_id);
  if (device == nullptr) {
    VLOG(1) << "Device " << device_id << " no longer in adapter";
    RecordRequestDeviceOutcome(UMARequestDeviceOutcome::CHOSEN_DEVICE_VANISHED);
    Send(new BluetoothMsg_RequestDeviceError(
        session->thread_id, session->request_id,
        WebBluetoothError::CHOSEN_DEVICE_VANISHED));
    request_device_sessions_.Remove(chooser_id);
    return;
  }

  const std::string& device_id_for_origin = allowed_devices_map_.AddDevice(
      session->origin, device->GetAddress(), session->filters,
      session->optional_services);

  VLOG(1) << "Device: " << device->GetName();
  VLOG(1) << "UUIDs: ";

  device::BluetoothDevice::UUIDList filtered_uuids;
  for (BluetoothUUID uuid : device->GetUUIDs()) {
    if (allowed_devices_map_.IsOriginAllowedToAccessService(
            session->origin, device_id_for_origin, uuid.canonical_value())) {
      VLOG(1) << "\t Allowed: " << uuid.canonical_value();
      filtered_uuids.push_back(uuid);
    } else {
      VLOG(1) << "\t Not Allowed: " << uuid.canonical_value();
    }
  }

  content::BluetoothDevice device_ipc(
      device_id_for_origin,  // id
      device->GetName(),     // name
      content::BluetoothDevice::UUIDsFromBluetoothUUIDs(
          filtered_uuids));  // uuids
  RecordRequestDeviceOutcome(UMARequestDeviceOutcome::SUCCESS);
  Send(new BluetoothMsg_RequestDeviceSuccess(session->thread_id,
                                             session->request_id, device_ipc));
  request_device_sessions_.Remove(chooser_id);
}

CacheQueryResult BluetoothDispatcherHost::QueryCacheForDevice(
    const url::Origin& origin,
    const std::string& device_id) {
  const std::string& device_address =
      allowed_devices_map_.GetDeviceAddress(origin, device_id);
  if (device_address.empty()) {
    bad_message::ReceivedBadMessage(
        this, bad_message::BDH_DEVICE_NOT_ALLOWED_FOR_ORIGIN);
    return CacheQueryResult(CacheQueryOutcome::BAD_RENDERER);
  }

  CacheQueryResult result;
  result.device = adapter_->GetDevice(device_address);

  // When a device can't be found in the BluetoothAdapter, that generally
  // indicates that it's gone out of range. We reject with a NetworkError in
  // that case.
  // https://webbluetoothchrome.github.io/web-bluetooth/#dom-bluetoothdevice-connectgatt
  if (result.device == nullptr) {
    result.outcome = CacheQueryOutcome::NO_DEVICE;
  }
  return result;
}

void BluetoothDispatcherHost::AddAdapterObserver(
    device::BluetoothAdapter::Observer* observer) {
  adapter_observers_.insert(observer);
  if (adapter_) {
    adapter_->AddObserver(observer);
  }
}

void BluetoothDispatcherHost::RemoveAdapterObserver(
    device::BluetoothAdapter::Observer* observer) {
  size_t removed = adapter_observers_.erase(observer);
  DCHECK(removed);
  if (adapter_) {
    adapter_->RemoveObserver(observer);
  }
}

}  // namespace content
