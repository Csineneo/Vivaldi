// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_use_measurement/content/data_use_measurement.h"

#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "content/public/browser/resource_request_info.h"
#include "net/base/network_change_notifier.h"
#include "net/base/upload_data_stream.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request.h"

#if defined(OS_ANDROID)
#include "net/android/traffic_stats.h"
#endif

namespace data_use_measurement {

namespace {

// Records the occurrence of |sample| in |name| histogram. Conventional UMA
// histograms are not used because the |name| is not static.
void RecordUMAHistogramCount(const std::string& name, int64_t sample) {
  base::HistogramBase* histogram_pointer = base::Histogram::FactoryGet(
      name,
      1,        // Minimum sample size in bytes.
      1000000,  // Maximum sample size in bytes. Should cover most of the
                // requests by services.
      50,       // Bucket count.
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram_pointer->Add(sample);
}

// This function increases the value of |sample| bucket in |name| sparse
// histogram by |value|. Conventional UMA histograms are not used because |name|
// is not static.
void IncreaseSparseHistogramByValue(const std::string& name,
                                    int64_t sample,
                                    int64_t value) {
  base::HistogramBase* histogram = base::SparseHistogram::FactoryGet(
      name, base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->AddCount(sample, value);
}

#if defined(OS_ANDROID)
void IncrementLatencyHistogramByCount(const std::string& name,
                                      const base::TimeDelta& latency,
                                      int64_t count) {
  base::HistogramBase* histogram_pointer = base::Histogram::FactoryTimeGet(
      name,
      base::TimeDelta::FromMilliseconds(1),  // Minimum sample
      base::TimeDelta::FromHours(1),         // Maximum sample
      50,                                    // Bucket count.
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram_pointer->AddCount(latency.InMilliseconds(), count);
}
#endif

}  // namespace

DataUseMeasurement::DataUseMeasurement(
    const metrics::UpdateUsagePrefCallbackType& metrics_data_use_forwarder)
    : metrics_data_use_forwarder_(metrics_data_use_forwarder)
#if defined(OS_ANDROID)
      ,
      app_state_(base::android::APPLICATION_STATE_HAS_RUNNING_ACTIVITIES),
      app_listener_(new base::android::ApplicationStatusListener(
          base::Bind(&DataUseMeasurement::OnApplicationStateChange,
                     base::Unretained(this)))),
      rx_bytes_os_(0),
      tx_bytes_os_(0),
      bytes_transferred_since_last_traffic_stats_query_(0),
      no_reads_since_background_(false)
#endif
{
}

DataUseMeasurement::~DataUseMeasurement(){};

void DataUseMeasurement::OnBeforeURLRequest(net::URLRequest* request) {
  DataUseUserData* data_use_user_data = reinterpret_cast<DataUseUserData*>(
      request->GetUserData(DataUseUserData::kUserDataKey));
  if (!data_use_user_data) {
    data_use_user_data = new DataUseUserData(
        DataUseUserData::ServiceName::NOT_TAGGED, CurrentAppState());
    request->SetUserData(DataUseUserData::kUserDataKey, data_use_user_data);
  }
}

void DataUseMeasurement::OnBeforeRedirect(const net::URLRequest& request,
                                          const GURL& new_location) {
  // Recording data use of request on redirects.
  // TODO(rajendrant): May not be needed when http://crbug/651957 is fixed.
  UpdateDataUsePrefs(request);
}

void DataUseMeasurement::OnNetworkBytesReceived(const net::URLRequest& request,
                                                int64_t bytes_received) {
  UMA_HISTOGRAM_COUNTS("DataUse.BytesReceived.Delegate", bytes_received);
  ReportDataUseUMA(request, DOWNSTREAM, bytes_received);
#if defined(OS_ANDROID)
  bytes_transferred_since_last_traffic_stats_query_ += bytes_received;
#endif
}

void DataUseMeasurement::OnNetworkBytesSent(const net::URLRequest& request,
                                            int64_t bytes_sent) {
  UMA_HISTOGRAM_COUNTS("DataUse.BytesSent.Delegate", bytes_sent);
  ReportDataUseUMA(request, UPSTREAM, bytes_sent);
#if defined(OS_ANDROID)
  bytes_transferred_since_last_traffic_stats_query_ += bytes_sent;
#endif
}

void DataUseMeasurement::OnCompleted(const net::URLRequest& request,
                                     bool started) {
  // TODO(amohammadkhan): Verify that there is no double recording in data use
  // of redirected requests.
  UpdateDataUsePrefs(request);
#if defined(OS_ANDROID)
  MaybeRecordNetworkBytesOS();
#endif
}

void DataUseMeasurement::ReportDataUseUMA(const net::URLRequest& request,
                                          TrafficDirection dir,
                                          int64_t bytes) {
  bool is_user_traffic = IsUserInitiatedRequest(request);
  bool is_connection_cellular =
      net::NetworkChangeNotifier::IsConnectionCellular(
          net::NetworkChangeNotifier::GetConnectionType());

  DataUseUserData* attached_service_data = static_cast<DataUseUserData*>(
      request.GetUserData(DataUseUserData::kUserDataKey));
  DataUseUserData::ServiceName service_name = DataUseUserData::NOT_TAGGED;
  DataUseUserData::AppState old_app_state = DataUseUserData::FOREGROUND;
  DataUseUserData::AppState new_app_state = DataUseUserData::UNKNOWN;

  if (attached_service_data) {
    service_name = attached_service_data->service_name();
    old_app_state = attached_service_data->app_state();
  }
  if (old_app_state == CurrentAppState())
    new_app_state = old_app_state;

  if (attached_service_data && old_app_state != new_app_state)
    attached_service_data->set_app_state(CurrentAppState());

  RecordUMAHistogramCount(
      GetHistogramName(is_user_traffic ? "DataUse.TrafficSize.User"
                                       : "DataUse.TrafficSize.System",
                       dir, new_app_state, is_connection_cellular),
      bytes);

  if (!is_user_traffic) {
    ReportDataUsageServices(service_name, dir, new_app_state,
                            is_connection_cellular, bytes);
  }
#if defined(OS_ANDROID)
  if (dir == DOWNSTREAM && CurrentAppState() == DataUseUserData::BACKGROUND) {
    DCHECK(!last_app_background_time_.is_null());

    const base::TimeDelta time_since_background =
        base::TimeTicks::Now() - last_app_background_time_;
    IncrementLatencyHistogramByCount(
        is_user_traffic ? "DataUse.BackgroundToDataRecievedPerByte.User"
                        : "DataUse.BackgroundToDataRecievedPerByte.System",
        time_since_background, bytes);
    if (no_reads_since_background_) {
      no_reads_since_background_ = false;
      IncrementLatencyHistogramByCount(
          is_user_traffic ? "DataUse.BackgroundToFirstDownstream.User"
                          : "DataUse.BackgroundToFirstDownstream.System",
          time_since_background, 1);
    }
  }
#endif
}

void DataUseMeasurement::UpdateDataUsePrefs(
    const net::URLRequest& request) const {
  bool is_connection_cellular =
      net::NetworkChangeNotifier::IsConnectionCellular(
          net::NetworkChangeNotifier::GetConnectionType());

  DataUseUserData* attached_service_data = static_cast<DataUseUserData*>(
      request.GetUserData(DataUseUserData::kUserDataKey));
  DataUseUserData::ServiceName service_name =
      attached_service_data ? attached_service_data->service_name()
                            : DataUseUserData::NOT_TAGGED;

  // Update data use prefs for cellular connections.
  if (!metrics_data_use_forwarder_.is_null()) {
    metrics_data_use_forwarder_.Run(
        DataUseUserData::GetServiceNameAsString(service_name),
        request.GetTotalSentBytes() + request.GetTotalReceivedBytes(),
        is_connection_cellular);
  }
}

// static
bool DataUseMeasurement::IsUserInitiatedRequest(
    const net::URLRequest& request) {
  // Having ResourceRequestInfo in the URL request is a sign that the request is
  // for a web content from user. For now we could add a condition to check
  // ProcessType in info is content::PROCESS_TYPE_RENDERER, but it won't be
  // compatible with upcoming PlzNavigate architecture. So just existence of
  // ResourceRequestInfo is verified, and the current check should be compatible
  // with upcoming changes in PlzNavigate.
  // TODO(rajendrant): Verify this condition for different use cases. See
  // crbug.com/626063.
  return content::ResourceRequestInfo::ForRequest(&request) != nullptr;
}

#if defined(OS_ANDROID)
void DataUseMeasurement::OnApplicationStateChangeForTesting(
    base::android::ApplicationState application_state) {
  OnApplicationStateChange(application_state);
}
#endif

DataUseUserData::AppState DataUseMeasurement::CurrentAppState() const {
#if defined(OS_ANDROID)
  if (app_state_ != base::android::APPLICATION_STATE_HAS_RUNNING_ACTIVITIES)
    return DataUseUserData::BACKGROUND;
#endif
  // If the OS is not Android, all the requests are considered Foreground.
  return DataUseUserData::FOREGROUND;
}

std::string DataUseMeasurement::GetHistogramName(
    const char* prefix,
    TrafficDirection dir,
    DataUseUserData::AppState app_state,
    bool is_connection_cellular) const {
  return base::StringPrintf(
      "%s.%s.%s.%s", prefix, dir == UPSTREAM ? "Upstream" : "Downstream",
      app_state == DataUseUserData::UNKNOWN
          ? "Unknown"
          : (app_state == DataUseUserData::FOREGROUND ? "Foreground"
                                                      : "Background"),
      is_connection_cellular ? "Cellular" : "NotCellular");
}

#if defined(OS_ANDROID)
void DataUseMeasurement::OnApplicationStateChange(
    base::android::ApplicationState application_state) {
  app_state_ = application_state;
  if (app_state_ != base::android::APPLICATION_STATE_HAS_RUNNING_ACTIVITIES) {
    last_app_background_time_ = base::TimeTicks::Now();
    no_reads_since_background_ = true;
    MaybeRecordNetworkBytesOS();
  } else {
    last_app_background_time_ = base::TimeTicks();
  }
}

void DataUseMeasurement::MaybeRecordNetworkBytesOS() {
  // Minimum number of bytes that should be reported by the network delegate
  // before Android's TrafficStats API is queried (if Chrome is not in
  // background). This reduces the overhead of repeatedly calling the API.
  static const int64_t kMinDelegateBytes = 25000;

  if (bytes_transferred_since_last_traffic_stats_query_ < kMinDelegateBytes &&
      CurrentAppState() == DataUseUserData::FOREGROUND) {
    return;
  }
  bytes_transferred_since_last_traffic_stats_query_ = 0;
  int64_t bytes = 0;
  // Query Android traffic stats directly instead of registering with the
  // DataUseAggregator since the latter does not provide notifications for
  // the incognito traffic.
  if (net::android::traffic_stats::GetCurrentUidRxBytes(&bytes)) {
    if (rx_bytes_os_ != 0) {
      DCHECK_GE(bytes, rx_bytes_os_);
      UMA_HISTOGRAM_COUNTS("DataUse.BytesReceived.OS", bytes - rx_bytes_os_);
    }
    rx_bytes_os_ = bytes;
  }

  if (net::android::traffic_stats::GetCurrentUidTxBytes(&bytes)) {
    if (tx_bytes_os_ != 0) {
      DCHECK_GE(bytes, tx_bytes_os_);
      UMA_HISTOGRAM_COUNTS("DataUse.BytesSent.OS", bytes - tx_bytes_os_);
    }
    tx_bytes_os_ = bytes;
  }
}
#endif

void DataUseMeasurement::ReportDataUsageServices(
    DataUseUserData::ServiceName service,
    TrafficDirection dir,
    DataUseUserData::AppState app_state,
    bool is_connection_cellular,
    int64_t message_size) const {
  RecordUMAHistogramCount(
      "DataUse.MessageSize." + DataUseUserData::GetServiceNameAsString(service),
      message_size);
  if (message_size > 0) {
    IncreaseSparseHistogramByValue(
        GetHistogramName("DataUse.MessageSize.AllServices", dir, app_state,
                         is_connection_cellular),
        service, message_size);
  }
}

}  // namespace data_use_measurement
