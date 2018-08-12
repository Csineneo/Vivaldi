// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_delegate.h"

#include <cmath>

#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_bypass_stats.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_configurator.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_request_options.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_event_creator.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params.h"
#include "net/base/host_port_pair.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/proxy/proxy_server.h"
#include "net/proxy/proxy_service.h"

namespace data_reduction_proxy {

DataReductionProxyDelegate::DataReductionProxyDelegate(
    DataReductionProxyRequestOptions* request_options,
    DataReductionProxyConfig* config,
    const DataReductionProxyConfigurator* configurator,
    DataReductionProxyEventCreator* event_creator,
    DataReductionProxyBypassStats* bypass_stats,
    net::NetLog* net_log)
    : request_options_(request_options),
      config_(config),
      configurator_(configurator),
      event_creator_(event_creator),
      bypass_stats_(bypass_stats),
      net_log_(net_log) {
  DCHECK(request_options);
  DCHECK(config);
  DCHECK(configurator);
  DCHECK(event_creator);
  DCHECK(bypass_stats);
  DCHECK(net_log);
}

DataReductionProxyDelegate::~DataReductionProxyDelegate() {
}

void DataReductionProxyDelegate::OnResolveProxy(
    const GURL& url,
    const std::string& method,
    int load_flags,
    const net::ProxyService& proxy_service,
    net::ProxyInfo* result) {
  DCHECK(result);
  OnResolveProxyHandler(url, method, load_flags,
                        configurator_->GetProxyConfig(),
                        proxy_service.proxy_retry_info(), config_, result);
}

void DataReductionProxyDelegate::OnTunnelConnectCompleted(
    const net::HostPortPair& endpoint,
    const net::HostPortPair& proxy_server,
    int net_error) {
  if (config_->IsDataReductionProxy(proxy_server, NULL)) {
    UMA_HISTOGRAM_SPARSE_SLOWLY("DataReductionProxy.HTTPConnectCompleted",
                                std::abs(net_error));
  }
}

void DataReductionProxyDelegate::OnFallback(const net::ProxyServer& bad_proxy,
                                            int net_error) {
  if (bad_proxy.is_valid() &&
      config_->IsDataReductionProxy(bad_proxy.host_port_pair(), nullptr)) {
    event_creator_->AddProxyFallbackEvent(net_log_, bad_proxy.ToURI(),
                                          net_error);
  }

  if (bypass_stats_)
    bypass_stats_->OnProxyFallback(bad_proxy, net_error);
}

void DataReductionProxyDelegate::OnBeforeSendHeaders(
    net::URLRequest* request,
    const net::ProxyInfo& proxy_info,
    net::HttpRequestHeaders* headers) {
}

void DataReductionProxyDelegate::OnBeforeTunnelRequest(
    const net::HostPortPair& proxy_server,
    net::HttpRequestHeaders* extra_headers) {
  request_options_->MaybeAddProxyTunnelRequestHandler(
      proxy_server, extra_headers);
}

bool DataReductionProxyDelegate::IsTrustedSpdyProxy(
    const net::ProxyServer& proxy_server) {
  if (!proxy_server.is_https() ||
      !params::IsIncludedInTrustedSpdyProxyFieldTrial() ||
      !proxy_server.is_valid()) {
    return false;
  }
  return config_ &&
         config_->IsDataReductionProxy(proxy_server.host_port_pair(), nullptr);
}

void DataReductionProxyDelegate::OnTunnelHeadersReceived(
    const net::HostPortPair& origin,
    const net::HostPortPair& proxy_server,
    const net::HttpResponseHeaders& response_headers) {
}

void OnResolveProxyHandler(const GURL& url,
                           const std::string& method,
                           int load_flags,
                           const net::ProxyConfig& data_reduction_proxy_config,
                           const net::ProxyRetryInfoMap& proxy_retry_info,
                           const DataReductionProxyConfig* config,
                           net::ProxyInfo* result) {
  DCHECK(config);
  DCHECK(result->is_empty() || result->is_direct() ||
         !config->IsDataReductionProxy(result->proxy_server().host_port_pair(),
                                       NULL));
  bool data_saver_proxy_used = true;
  if (result->is_empty() || !result->proxy_server().is_direct() ||
      result->proxy_list().size() != 1 || url.SchemeIsWSOrWSS() ||
      method == "POST") {
    return;
  }

  if (data_reduction_proxy_config.is_valid()) {
    net::ProxyInfo data_reduction_proxy_info;
    data_reduction_proxy_config.proxy_rules().Apply(url,
                                                    &data_reduction_proxy_info);
    data_reduction_proxy_info.DeprioritizeBadProxies(proxy_retry_info);
    if (!data_reduction_proxy_info.proxy_server().is_direct())
      result->OverrideProxyList(data_reduction_proxy_info.proxy_list());
  } else {
    data_saver_proxy_used = false;
  }
  if (config->enabled_by_user_and_reachable() && url.SchemeIsHTTPOrHTTPS() &&
      !url.SchemeIsCryptographic() && !net::IsLocalhost(url.host())) {
    UMA_HISTOGRAM_BOOLEAN("DataReductionProxy.ConfigService.HTTPRequests",
                          data_saver_proxy_used);
  }
}

}  // namespace data_reduction_proxy
