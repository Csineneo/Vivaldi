// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/offline_pages/background/request_coordinator_event_logger.h"

namespace offline_pages {

namespace {

static std::string OfflinerRequestStatusToString(
    Offliner::RequestStatus request_status) {
  switch (request_status) {
    case Offliner::UNKNOWN:
      return "UNKNOWN";
    case Offliner::LOADED:
      return "LOADED";
    case Offliner::SAVED:
      return "SAVED";
    case Offliner::REQUEST_COORDINATOR_CANCELED:
      return "REQUEST_COORDINATOR_CANCELED";
    case Offliner::PRERENDERING_CANCELED:
      return "PRERENDERING_CANCELED";
    case Offliner::PRERENDERING_FAILED:
      return "PRERENDERING_FAILED";
    case Offliner::SAVE_FAILED:
      return "SAVE_FAILED";
    case Offliner::FOREGROUND_CANCELED:
      return "FOREGROUND_CANCELED";
    case Offliner::REQUEST_COORDINATOR_TIMED_OUT:
      return "REQUEST_COORDINATOR_TIMED_OUT";
    case Offliner::PRERENDERING_NOT_STARTED:
      return "PRERENDERING_NOT_STARTED";
    case Offliner::PRERENDERING_FAILED_NO_RETRY:
      return "PRERENDERING_FAILED_NO_RETRY";
    default:
      NOTREACHED();
      return std::to_string(static_cast<int>(request_status));
  }
}

static std::string BackgroundSavePageResultToString(
    RequestNotifier::BackgroundSavePageResult result) {
  switch (result) {
    case RequestNotifier::BackgroundSavePageResult::SUCCESS:
      return "SUCCESS";
    case RequestNotifier::BackgroundSavePageResult::PRERENDER_FAILURE:
      return "PRERENDER_FAILURE";
    case RequestNotifier::BackgroundSavePageResult::PRERENDER_CANCELED:
      return "PRERENDER_CANCELED";
    case RequestNotifier::BackgroundSavePageResult::FOREGROUND_CANCELED:
      return "FOREGROUND_CANCELED";
    case RequestNotifier::BackgroundSavePageResult::SAVE_FAILED:
      return "SAVE_FAILED";
    case RequestNotifier::BackgroundSavePageResult::EXPIRED:
      return "EXPIRED";
    case RequestNotifier::BackgroundSavePageResult::RETRY_COUNT_EXCEEDED:
      return "RETRY_COUNT_EXCEEDED";
    case RequestNotifier::BackgroundSavePageResult::START_COUNT_EXCEEDED:
      return "START_COUNT_EXCEEDED";
    case RequestNotifier::BackgroundSavePageResult::REMOVED:
      return "REMOVED";
    default:
      NOTREACHED();
      return std::to_string(static_cast<int>(result));
  }
}

static std::string UpdateRequestResultToString(
    RequestQueue::UpdateRequestResult result) {
  switch (result) {
    case RequestQueue::UpdateRequestResult::SUCCESS:
      return "SUCCESS";
    case RequestQueue::UpdateRequestResult::STORE_FAILURE:
      return "STORE_FAILURE";
    case RequestQueue::UpdateRequestResult::REQUEST_DOES_NOT_EXIST:
      return "REQUEST_DOES_NOT_EXIST";
    default:
      NOTREACHED();
      return std::to_string(static_cast<int>(result));
  }
}

}  // namespace

void RequestCoordinatorEventLogger::RecordOfflinerResult(
    const std::string& name_space,
    Offliner::RequestStatus new_status,
    int64_t request_id) {
  RecordActivity("Background save attempt for " + name_space + ":" +
                 std::to_string(request_id) + " - " +
                 OfflinerRequestStatusToString(new_status));
}

void RequestCoordinatorEventLogger::RecordDroppedSavePageRequest(
    const std::string& name_space,
    RequestNotifier::BackgroundSavePageResult result,
    int64_t request_id) {
  RecordActivity("Background save request removed " + name_space + ":" +
                 std::to_string(request_id) + " - " +
                 BackgroundSavePageResultToString(result));
}

void RequestCoordinatorEventLogger::RecordUpdateRequestFailed(
    const std::string& name_space,
    RequestQueue::UpdateRequestResult result) {
  RecordActivity("Updating queued request for " + name_space + " failed - " +
                 UpdateRequestResultToString(result));
}

}  // namespace offline_pages
