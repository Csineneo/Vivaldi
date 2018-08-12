// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OFFLINE_PAGES_BACKGROUND_REQUEST_COORDINATOR_H_
#define COMPONENTS_OFFLINE_PAGES_BACKGROUND_REQUEST_COORDINATOR_H_

#include <memory>
#include <set>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/offline_pages/background/device_conditions.h"
#include "components/offline_pages/background/offliner.h"
#include "components/offline_pages/background/request_coordinator_event_logger.h"
#include "components/offline_pages/background/request_notifier.h"
#include "components/offline_pages/background/request_queue.h"
#include "components/offline_pages/background/scheduler.h"
#include "net/nqe/network_quality_estimator.h"
#include "url/gurl.h"

namespace offline_pages {

struct ClientId;
class OfflinerPolicy;
class OfflinerFactory;
class Offliner;
class RequestPicker;
class SavePageRequest;
class Scheduler;
class ClientPolicyController;

// Coordinates queueing and processing save page later requests.
class RequestCoordinator : public KeyedService,
                           public RequestNotifier,
                           public base::SupportsUserData {
 public:

  // Nested observer class.  To make sure that no events are missed, the client
  // code should first register for notifications, then |GetAllRequests|, and
  // ignore all events before the return from |GetAllRequests|, and consume
  // events after the return callback from |GetAllRequests|.
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void OnAdded(const SavePageRequest& request) = 0;
    virtual void OnCompleted(
        const SavePageRequest& request,
        RequestNotifier::BackgroundSavePageResult status) = 0;
    virtual void OnChanged(const SavePageRequest& request) = 0;
  };

  enum class RequestAvailability {
    ENABLED_FOR_OFFLINER,
    DISABLED_FOR_OFFLINER,
  };

  // Callback to report when a request was available.
  typedef base::Callback<void(const SavePageRequest& request)>
      RequestPickedCallback;

  // Callback to report when no request was available.
  typedef base::Callback<void(bool)> RequestNotPickedCallback;

  // Callback specifying which request IDs were actually removed.
  typedef base::Callback<void(const MultipleItemStatuses&)>
      RemoveRequestsCallback;

  // Callback that receives the response for GetAllRequests.
  typedef base::Callback<void(std::vector<std::unique_ptr<SavePageRequest>>)>
      GetRequestsCallback;

  RequestCoordinator(std::unique_ptr<OfflinerPolicy> policy,
                     std::unique_ptr<OfflinerFactory> factory,
                     std::unique_ptr<RequestQueue> queue,
                     std::unique_ptr<Scheduler> scheduler,
                     net::NetworkQualityEstimator::NetworkQualityProvider*
                         network_quality_estimator);

  ~RequestCoordinator() override;

  // Queues |request| to later load and save when system conditions allow.
  // Returns an id if the page could be queued successfully, 0L otherwise.
  int64_t SavePageLater(const GURL& url,
                        const ClientId& client_id,
                        bool user_requested,
                        RequestAvailability availability);

  // Remove a list of requests by |request_id|.  This removes requests from the
  // request queue, and cancels an in-progress prerender.
  void RemoveRequests(const std::vector<int64_t>& request_ids,
                      const RemoveRequestsCallback& callback);

  // Pause a list of requests by |request_id|.  This will change the state
  // in the request queue so the request cannot be started.
  void PauseRequests(const std::vector<int64_t>& request_ids);

  // Resume a list of previously paused requests, making them available.
  void ResumeRequests(const std::vector<int64_t>& request_ids);

  // Get all save page request items in the callback.
  void GetAllRequests(const GetRequestsCallback& callback);

  // Starts processing of one or more queued save page later requests.
  // Returns whether processing was started and that caller should expect
  // a callback. If processing was already active, returns false.
  bool StartProcessing(const DeviceConditions& device_conditions,
                       const base::Callback<void(bool)>& callback);

  // Stops the current request processing if active. This is a way for
  // caller to abort processing; otherwise, processing will complete on
  // its own. In either case, the callback will be called when processing
  // is stopped or complete.
  void StopProcessing(Offliner::RequestStatus stop_status);

  // Used to denote that the foreground thread is ready for the offliner
  // to start work on a previously entered, but unavailable request.
  void EnableForOffliner(int64_t request_id);

  // If a request that is unavailable to the offliner is finished elsewhere,
  // (by the tab helper synchronous download), send a notificaiton that it
  // succeeded through our notificaiton system.
  void MarkRequestCompleted(int64_t request_id);

  const Scheduler::TriggerConditions GetTriggerConditions(
      const bool user_requested);

  // A way for tests to set the callback in use when an operation is over.
  void SetProcessingCallbackForTest(const base::Callback<void(bool)> callback) {
    scheduler_callback_ = callback;
  }

  // Observers implementing the RequestCoordinator::Observer interface can
  // register here to get notifications of changes to request state.  This
  // pointer is not owned, and it is the callers responsibility to remove the
  // observer before the observer is deleted.
  void AddObserver(RequestCoordinator::Observer* observer);

  void RemoveObserver(RequestCoordinator::Observer* observer);

  // Implement RequestNotifier
  void NotifyAdded(const SavePageRequest& request) override;
  void NotifyCompleted(
      const SavePageRequest& request,
      RequestNotifier::BackgroundSavePageResult status) override;
  void NotifyChanged(const SavePageRequest& request) override;

  // Returns the request queue used for requests.  Coordinator keeps ownership.
  RequestQueue* queue() { return queue_.get(); }

  // Return an unowned pointer to the Scheduler.
  Scheduler* scheduler() { return scheduler_.get(); }

  OfflinerPolicy* policy() { return policy_.get(); }

  ClientPolicyController* GetPolicyController();

  // Returns the status of the most recent offlining.
  Offliner::RequestStatus last_offlining_status() {
    return last_offlining_status_;
  }

  bool is_busy() {
    return is_busy_;
  }

  // Returns whether processing is starting (before it is decided to actually
  // process a request (is_busy()) at this time or not.
  bool is_starting() { return is_starting_; }

  // Tracks whether the last offlining attempt got canceled.  This is reset by
  // the next StartProcessing() call.
  bool is_canceled() {
    return is_stopped_;
  }

  OfflineEventLogger* GetLogger() {
    return &event_logger_;
  }

 private:
  // Immediate start attempt status code for UMA.
  // For any changes, also update corresponding histogram in histograms.xml.
  enum OfflinerImmediateStartStatus {
    // Did start processing request.
    STARTED = 0,
    // Already busy processing a request.
    BUSY = 1,
    // The Offliner did not accept processing the request.
    NOT_ACCEPTED = 2,
    // No current network connection.
    NO_CONNECTION = 3,
    // Weak network connection (worse than 2G speed)
    // according to network quality estimator.
    WEAK_CONNECTION = 4,
    // Did not start because this is svelte device.
    NOT_STARTED_ON_SVELTE = 5,
    // NOTE: insert new values above this line and update histogram enum too.
    STATUS_COUNT = 6,
  };

  // Receives the results of a get from the request queue, and turns that into
  // SavePageRequest objects for the caller of GetQueuedRequests.
  void GetQueuedRequestsCallback(
      const GetRequestsCallback& callback,
      RequestQueue::GetRequestsResult result,
      std::vector<std::unique_ptr<SavePageRequest>> requests);

  // Receives the results of a get from the request queue, and turns that into
  // SavePageRequest objects for the caller of GetQueuedRequests.
  void GetRequestsForSchedulingCallback(
      RequestQueue::GetRequestsResult result,
      std::vector<std::unique_ptr<SavePageRequest>> requests);

  // Receives the result of add requests to the request queue.
  void AddRequestResultCallback(RequestQueue::AddRequestResult result,
                                const SavePageRequest& request);

  // Receives the result of update and delete requests to the request queue.
  void UpdateRequestCallback(const ClientId& client_id,
                             RequestQueue::UpdateRequestResult result);

  void UpdateMultipleRequestsCallback(
      std::unique_ptr<UpdateRequestsResult> result);

  void CompletedRequestCallback(const MultipleItemStatuses& status);

  void HandleRemovedRequestsAndCallback(
      const RemoveRequestsCallback& callback,
      BackgroundSavePageResult status,
      std::unique_ptr<UpdateRequestsResult> result);

  void HandleRemovedRequests(BackgroundSavePageResult status,
                             std::unique_ptr<UpdateRequestsResult> result);

  // Start processing now if connected (but with conservative assumption
  // as to other device conditions).
  void StartProcessingIfConnected();

  OfflinerImmediateStartStatus TryImmediateStart();

  // Check the request queue, and schedule a task corresponding
  // to the least restrictive type of request in the queue.
  void ScheduleAsNeeded();

  // Callback from the request picker when it has chosen our next request.
  void RequestPicked(const SavePageRequest& request);

  // Callback from the request picker when no more requests are in the queue.
  // The parameter is a signal for what (if any) conditions to schedule future
  // processing for.
  void RequestNotPicked(bool non_user_requested_tasks_remaining);

  void HandleWatchdogTimeout();

  // Cancels an in progress pre-rendering, and updates state appropriately.
  void StopPrerendering(Offliner::RequestStatus stop_status);

  void SendRequestToOffliner(const SavePageRequest& request);

  // Called by the offliner when an offlining request is completed. (and by
  // tests).
  void OfflinerDoneCallback(const SavePageRequest& request,
                            Offliner::RequestStatus status);

  void TryNextRequest();

  // If there is an active request in the list, cancel that request.
  bool CancelActiveRequestIfItMatches(const std::vector<int64_t>& request_ids);

  // Records an aborted attempt for the request and update it in the queue
  // (possibly removing it). Returns the updated copy.
  void AbortRequestAttempt(SavePageRequest* request);

  // Remove the attempted request from the queue with status to pass through to
  // any observers and UMA histogram.
  void RemoveAttemptedRequest(const SavePageRequest& request,
                              BackgroundSavePageResult status);

  // Returns the appropriate offliner to use, getting a new one from the factory
  // if needed.
  void GetOffliner();

  // Method to wrap calls to getting the connection type so it can be
  // changed for tests.
  net::NetworkChangeNotifier::ConnectionType GetConnectionType();

  void SetNetworkConditionsForTest(
      net::NetworkChangeNotifier::ConnectionType connection) {
    use_test_connection_type_ = true;
    test_connection_type_ = connection;
  }

  void SetOfflinerTimeoutForTest(const base::TimeDelta& timeout) {
    offliner_timeout_ = timeout;
  }

  void SetDeviceConditionsForTest(DeviceConditions& current_conditions) {
    current_conditions_.reset(new DeviceConditions(current_conditions));
  }

  // KeyedService implementation:
  void Shutdown() override;

  friend class RequestCoordinatorTest;

  // The offliner can only handle one request at a time - if the offliner is
  // busy, prevent other requests.  This flag marks whether the offliner is in
  // use.
  bool is_busy_;
  // There is more than one path to start processing so this flag is used
  // to avoid race conditions before is_busy_ is established.
  bool is_starting_;
  // True if the current processing window has been canceled.
  bool is_stopped_;
  // True if we should use the test connection type instead of the actual type.
  bool use_test_connection_type_;
  // For use by tests, a fake network connection type
  net::NetworkChangeNotifier::ConnectionType test_connection_type_;
  // Unowned pointer to the current offliner, if any.
  Offliner* offliner_;
  base::Time operation_start_time_;
  // The observers.
  base::ObserverList<Observer> observers_;
  // Last known conditions for network, battery
  std::unique_ptr<DeviceConditions> current_conditions_;
  // RequestCoordinator takes over ownership of the policy
  std::unique_ptr<OfflinerPolicy> policy_;
  // OfflinerFactory.  Used to create offline pages. Owned.
  std::unique_ptr<OfflinerFactory> factory_;
  // RequestQueue.  Used to store incoming requests. Owned.
  std::unique_ptr<RequestQueue> queue_;
  // Scheduler. Used to request a callback when network is available.  Owned.
  std::unique_ptr<Scheduler> scheduler_;
  // Controller of client policies. Owned.
  std::unique_ptr<ClientPolicyController> policy_controller_;
  // Unowned pointer to the Network Quality Estimator.
  net::NetworkQualityEstimator::NetworkQualityProvider*
      network_quality_estimator_;
  // Holds copy of the active request, if any.
  std::unique_ptr<SavePageRequest> active_request_;
  // Status of the most recent offlining.
  Offliner::RequestStatus last_offlining_status_;
  // Class to choose which request to schedule next
  std::unique_ptr<RequestPicker> picker_;
  // A set of request_ids that we are holding off until the download manager is
  // done with them.
  std::set<int64_t> disabled_requests_;
  // Calling this returns to the scheduler across the JNI bridge.
  base::Callback<void(bool)> scheduler_callback_;
  // Logger to record events.
  RequestCoordinatorEventLogger event_logger_;
  // Timer to watch for pre-render attempts running too long.
  base::OneShotTimer watchdog_timer_;
  // How long to wait for an offliner request before giving up.
  base::TimeDelta offliner_timeout_;
  // Allows us to pass a weak pointer to callbacks.
  base::WeakPtrFactory<RequestCoordinator> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(RequestCoordinator);
};

}  // namespace offline_pages

#endif  // COMPONENTS_OFFLINE_PAGES_BACKGROUND_REQUEST_COORDINATOR_H_
