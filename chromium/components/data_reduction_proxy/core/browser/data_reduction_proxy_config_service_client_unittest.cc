// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config_service_client.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/run_loop.h"
#include "base/test/histogram_tester.h"
#include "base/test/mock_entropy_provider.h"
#include "base/time/time.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config_test_utils.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_configurator_test_utils.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_delegate.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_test_utils.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_client_config_parser.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params_test_utils.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_pref_names.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_switches.h"
#include "components/data_reduction_proxy/proto/client_config.pb.h"
#include "net/base/network_change_notifier.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/proxy/proxy_server.h"
#include "net/socket/socket_test_util.h"
#include "net/url_request/url_request_context_storage.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// The following values should match those in
// DataReductionProxyConfigServiceClientTest.config_:
const char kSuccessOrigin[] = "https://origin.net:443";
const char kSuccessFallback[] = "fallback.net:80";
const char kSuccessSessionKey[] = "SecretSessionKey";

// The following values should match those in
// DataReductionProxyConfigServiceClientTest.previous_config_:
const char kOldSuccessOrigin[] = "https://old.origin.net:443";
const char kOldSuccessFallback[] = "old.fallback.net:80";
const char kOldSuccessSessionKey[] = "OldSecretSessionKey";

// The following values should match those in
// DataReductionProxyConfigServiceClientTest.loaded_config_:
const char kPersistedOrigin[] = "https://persisted.net:443";
const char kPersistedFallback[] = "persisted.net:80";
const char kPersistedSessionKey[] = "PersistedSessionKey";

// Duration (in seconds) after which the config should be refreshed.
const int kConfigRefreshDurationSeconds = 600;

#if defined(OS_ANDROID)
// Maximum duration  to wait before fetching the config, while the application
// is in background.
const uint32_t kMaxBackgroundFetchIntervalSeconds = 6 * 60 * 60;  // 6 hours.
#endif

}  // namespace

namespace data_reduction_proxy {

namespace {

// Creates a new ClientConfig from the given parameters.
ClientConfig CreateConfig(const std::string& session_key,
                          int64_t expire_duration_seconds,
                          int64_t expire_duration_nanoseconds,
                          ProxyServer_ProxyScheme primary_scheme,
                          const std::string& primary_host,
                          int primary_port,
                          ProxyServer_ProxyScheme secondary_scheme,
                          const std::string& secondary_host,
                          int secondary_port) {
  ClientConfig config;

  config.set_session_key(session_key);
  config.mutable_refresh_duration()->set_seconds(expire_duration_seconds);
  config.mutable_refresh_duration()->set_nanos(expire_duration_nanoseconds);
  ProxyServer* primary_proxy =
      config.mutable_proxy_config()->add_http_proxy_servers();
  primary_proxy->set_scheme(primary_scheme);
  primary_proxy->set_host(primary_host);
  primary_proxy->set_port(primary_port);
  ProxyServer* secondary_proxy =
      config.mutable_proxy_config()->add_http_proxy_servers();
  secondary_proxy->set_scheme(secondary_scheme);
  secondary_proxy->set_host(secondary_host);
  secondary_proxy->set_port(secondary_port);

  return config;
}

// Takes |config| and returns the base64 encoding of its serialized byte stream.
std::string EncodeConfig(const ClientConfig& config) {
  std::string config_data;
  std::string encoded_data;
  EXPECT_TRUE(config.SerializeToString(&config_data));
  base::Base64Encode(config_data, &encoded_data);
  return encoded_data;
}

}  // namespace

class DataReductionProxyConfigServiceClientTest : public testing::Test {
 protected:
  DataReductionProxyConfigServiceClientTest() {
    context_.reset(new net::TestURLRequestContext(true));
    context_storage_.reset(new net::URLRequestContextStorage(context_.get()));
    mock_socket_factory_.reset(new net::MockClientSocketFactory());
  }

  void Init(bool use_mock_client_socket_factory) {
    if (!use_mock_client_socket_factory)
      mock_socket_factory_.reset(nullptr);
    test_context_ =
        DataReductionProxyTestContext::Builder()
            .WithParamsDefinitions(TestDataReductionProxyParams::HAS_EVERYTHING)
            .WithURLRequestContext(context_.get())
            .WithMockClientSocketFactory(mock_socket_factory_.get())
            .WithTestConfigurator()
            .WithMockRequestOptions()
            .WithTestConfigClient()
            .Build();

    context_->set_client_socket_factory(mock_socket_factory_.get());
    test_context_->AttachToURLRequestContext(context_storage_.get());
    delegate_ = test_context_->io_data()->CreateProxyDelegate();
    context_->set_proxy_delegate(delegate_.get());

    context_->Init();
    ResetBackoffEntryReleaseTime();
    test_context_->test_config_client()->SetNow(base::Time::UnixEpoch());
    test_context_->test_config_client()->SetEnabled(true);
    enabled_proxies_for_http_ =
        test_context_->test_params()->proxies_for_http();
    test_context_->test_config_client()->SetConfigServiceURL(
        GURL("http://configservice.com"));

    ASSERT_NE(nullptr, context_->network_delegate());
    // Set up the various test ClientConfigs.
    ClientConfig config =
        CreateConfig(kSuccessSessionKey, kConfigRefreshDurationSeconds, 0,
                     ProxyServer_ProxyScheme_HTTPS, "origin.net", 443,
                     ProxyServer_ProxyScheme_HTTP, "fallback.net", 80);
    config.SerializeToString(&config_);
    encoded_config_ = EncodeConfig(config);

    ClientConfig previous_config =
        CreateConfig(kOldSuccessSessionKey, kConfigRefreshDurationSeconds, 0,
                     ProxyServer_ProxyScheme_HTTPS, "old.origin.net", 443,
                     ProxyServer_ProxyScheme_HTTP, "old.fallback.net", 80);
    previous_config.SerializeToString(&previous_config_);

    ClientConfig persisted =
        CreateConfig(kPersistedSessionKey, kConfigRefreshDurationSeconds, 0,
                     ProxyServer_ProxyScheme_HTTPS, "persisted.net", 443,
                     ProxyServer_ProxyScheme_HTTP, "persisted.net", 80);
    loaded_config_ = EncodeConfig(persisted);

    success_reads_[0] = net::MockRead("HTTP/1.1 200 OK\r\n\r\n");
    success_reads_[1] =
        net::MockRead(net::ASYNC, config_.c_str(), config_.length());
    success_reads_[2] = net::MockRead(net::SYNCHRONOUS, net::OK);

    previous_success_reads_[0] = net::MockRead("HTTP/1.1 200 OK\r\n\r\n");
    previous_success_reads_[1] = net::MockRead(
        net::ASYNC, previous_config_.c_str(), previous_config_.length());
    previous_success_reads_[2] = net::MockRead(net::SYNCHRONOUS, net::OK);

    not_found_reads_[0] = net::MockRead("HTTP/1.1 404 Not found\r\n\r\n");
    not_found_reads_[1] = net::MockRead(net::SYNCHRONOUS, net::OK);
  }

  void SetDataReductionProxyEnabled(bool enabled) {
    test_context_->config()->SetStateForTest(enabled, true);
  }

  void ResetBackoffEntryReleaseTime() {
    config_client()->SetCustomReleaseTime(base::TimeTicks::UnixEpoch());
  }

  void VerifyRemoteSuccess() {
    std::vector<net::ProxyServer> expected_http_proxies;
    expected_http_proxies.push_back(net::ProxyServer::FromURI(
        kSuccessOrigin, net::ProxyServer::SCHEME_HTTP));
    expected_http_proxies.push_back(net::ProxyServer::FromURI(
        kSuccessFallback, net::ProxyServer::SCHEME_HTTP));
    EXPECT_EQ(base::TimeDelta::FromSeconds(kConfigRefreshDurationSeconds),
              config_client()->GetDelay());
    EXPECT_THAT(configurator()->proxies_for_http(),
                testing::ContainerEq(expected_http_proxies));
    EXPECT_TRUE(configurator()->proxies_for_https().empty());
    EXPECT_EQ(kSuccessSessionKey, request_options()->GetSecureSession());
    // The config should be persisted on the pref.
    EXPECT_EQ(encoded_config(), persisted_config());
  }

  void VerifyRemoteSuccessWithOldConfig() {
    std::vector<net::ProxyServer> expected_http_proxies;
    expected_http_proxies.push_back(net::ProxyServer::FromURI(
        kOldSuccessOrigin, net::ProxyServer::SCHEME_HTTP));
    expected_http_proxies.push_back(net::ProxyServer::FromURI(
        kOldSuccessFallback, net::ProxyServer::SCHEME_HTTP));
    EXPECT_EQ(base::TimeDelta::FromSeconds(kConfigRefreshDurationSeconds),
              config_client()->GetDelay());
    EXPECT_THAT(configurator()->proxies_for_http(),
                testing::ContainerEq(expected_http_proxies));
    EXPECT_TRUE(configurator()->proxies_for_https().empty());
    EXPECT_EQ(kOldSuccessSessionKey, request_options()->GetSecureSession());
  }

  void VerifySuccessWithLoadedConfig() {
    std::vector<net::ProxyServer> expected_http_proxies;
    expected_http_proxies.push_back(net::ProxyServer::FromURI(
        kPersistedOrigin, net::ProxyServer::SCHEME_HTTP));
    expected_http_proxies.push_back(net::ProxyServer::FromURI(
        kPersistedFallback, net::ProxyServer::SCHEME_HTTP));
    EXPECT_THAT(configurator()->proxies_for_http(),
                testing::ContainerEq(expected_http_proxies));
    EXPECT_TRUE(configurator()->proxies_for_https().empty());
    EXPECT_EQ(kPersistedSessionKey, request_options()->GetSecureSession());
  }

  TestDataReductionProxyConfigServiceClient* config_client() {
    return test_context_->test_config_client();
  }

  TestDataReductionProxyConfigurator* configurator() {
    return test_context_->test_configurator();
  }

  TestDataReductionProxyConfig* config() { return test_context_->config(); }

  MockDataReductionProxyRequestOptions* request_options() {
    return test_context_->mock_request_options();
  }

  const std::vector<net::ProxyServer>& enabled_proxies_for_http() const {
    return enabled_proxies_for_http_;
  }

  void RunUntilIdle() {
    test_context_->RunUntilIdle();
  }

  void AddMockSuccess() {
    socket_data_providers_.push_back(
        (make_scoped_ptr(new net::StaticSocketDataProvider(
            success_reads_, arraysize(success_reads_), nullptr, 0))));
    mock_socket_factory_->AddSocketDataProvider(
        socket_data_providers_.back().get());
  }

  void AddMockPreviousSuccess() {
    socket_data_providers_.push_back(
        (make_scoped_ptr(new net::StaticSocketDataProvider(
            previous_success_reads_, arraysize(previous_success_reads_),
            nullptr, 0))));
    mock_socket_factory_->AddSocketDataProvider(
        socket_data_providers_.back().get());
  }

  void AddMockFailure() {
    socket_data_providers_.push_back(
        (make_scoped_ptr(new net::StaticSocketDataProvider(
            not_found_reads_, arraysize(not_found_reads_), nullptr, 0))));
    mock_socket_factory_->AddSocketDataProvider(
        socket_data_providers_.back().get());
  }

  std::string persisted_config() const {
    return test_context_->pref_service()->GetString(
        prefs::kDataReductionProxyConfig);
  }

  const std::string& success_response() const { return config_; }

  const std::string& encoded_config() const { return encoded_config_; }

  const std::string& previous_success_response() const {
    return previous_config_;
  }

  void EnableQuic(bool enable) {
    test_context_->test_params()->EnableQuic(enable);
  }

  bool IsTrustedSpdyProxy(const net::ProxyServer& proxy_server) const {
    return delegate_->IsTrustedSpdyProxy(proxy_server);
  }

  const std::string& loaded_config() const { return loaded_config_; }

  net::TestURLRequestContext* test_url_request_context() const {
    return context_.get();
  }

 private:
  base::MessageLoopForIO message_loop_;
  scoped_ptr<net::TestURLRequestContext> context_;
  scoped_ptr<net::MockClientSocketFactory> mock_socket_factory_;

  scoped_ptr<DataReductionProxyTestContext> test_context_;
  scoped_ptr<DataReductionProxyRequestOptions> request_options_;
  std::vector<net::ProxyServer> enabled_proxies_for_http_;

  scoped_ptr<DataReductionProxyDelegate> delegate_;

  // A configuration from the current remote request. The encoded version is
  // also stored.
  std::string config_;
  std::string encoded_config_;

  // A configuration from a previous remote request.
  std::string previous_config_;

  // An encoded config that represents a previously saved configuration.
  std::string loaded_config_;

  // Mock socket data.
  std::vector<scoped_ptr<net::SocketDataProvider>> socket_data_providers_;

  // Mock socket reads.
  net::MockRead success_reads_[3];
  net::MockRead previous_success_reads_[3];
  net::MockRead not_found_reads_[2];

  scoped_ptr<net::URLRequestContextStorage> context_storage_;

  DISALLOW_COPY_AND_ASSIGN(DataReductionProxyConfigServiceClientTest);
};

// Tests the interaction of client config with QUIC field trial.
TEST_F(DataReductionProxyConfigServiceClientTest, QuicFieldTrial) {
  Init(true);
  const struct {
    bool enable_quic;
    bool enable_trusted_spdy_proxy_field_trial;
    std::string expected_primary_proxy;
    std::string expected_fallback_proxy;
    net::ProxyServer::Scheme expected_primary_proxy_scheme;
  } tests[] = {
      {false, false, kSuccessOrigin, kSuccessFallback,
       net::ProxyServer::SCHEME_HTTPS},
      {false, true, kSuccessOrigin, kSuccessFallback,
       net::ProxyServer::SCHEME_HTTPS},
      {true, true, kSuccessOrigin, kSuccessFallback,
       net::ProxyServer::SCHEME_QUIC},
  };

  for (size_t i = 0; i < arraysize(tests); ++i) {
    base::FieldTrialList field_trial_list(new base::MockEntropyProvider());
    base::FieldTrialList::CreateFieldTrial(
        params::GetTrustedSpdyProxyFieldTrialName(),
        tests[i].enable_trusted_spdy_proxy_field_trial ? "Enabled" : "Control");
    if (tests[i].enable_quic) {
      base::FieldTrialList::CreateFieldTrial(params::GetQuicFieldTrialName(),
                                             "Enabled");
    } else {
      base::FieldTrialList::CreateFieldTrial(params::GetQuicFieldTrialName(),
                                             "Control");
    }
    EnableQuic(tests[i].enable_quic);

    // Use a remote config.
    AddMockSuccess();

    SetDataReductionProxyEnabled(true);

    config_client()->RetrieveConfig();
    RunUntilIdle();
    EXPECT_EQ(base::TimeDelta::FromSeconds(kConfigRefreshDurationSeconds),
              config_client()->GetDelay())
        << i;

    // Verify that the proxies were set properly.
    std::vector<net::ProxyServer> proxies_for_http =
        configurator()->proxies_for_http();

    EXPECT_EQ(2U, proxies_for_http.size()) << i;
    EXPECT_EQ(net::ProxyServer(tests[i].expected_primary_proxy_scheme,
                               net::ProxyServer::FromURI(
                                   tests[i].expected_primary_proxy,
                                   tests[i].expected_primary_proxy_scheme)
                                   .host_port_pair()),
              proxies_for_http[0])
        << i;
    EXPECT_EQ(net::ProxyServer::FromURI(tests[i].expected_fallback_proxy,
                                        net::ProxyServer::SCHEME_HTTP),
              proxies_for_http[1])
        << i;
    EXPECT_TRUE(configurator()->proxies_for_https().empty()) << i;

    // Test that the trusted SPDY proxy is updated correctly after each config
    // retrieval.
    bool expect_proxy_is_trusted =
        tests[i].expected_primary_proxy_scheme ==
            net::ProxyServer::SCHEME_HTTPS &&
        tests[i].enable_trusted_spdy_proxy_field_trial;

    // Apply the specified proxy scheme.
    const net::ProxyServer proxy_server(
        tests[i].expected_primary_proxy_scheme,
        net::ProxyServer::FromURI(tests[i].expected_primary_proxy,
                                  net::ProxyServer::SCHEME_HTTP)
            .host_port_pair());

    ASSERT_EQ(tests[i].expected_primary_proxy_scheme, proxy_server.scheme())
        << i;
    EXPECT_EQ(expect_proxy_is_trusted, IsTrustedSpdyProxy(proxy_server)) << i;
  }
}

// Tests that backoff values increases with every time config cannot be fetched.
TEST_F(DataReductionProxyConfigServiceClientTest, EnsureBackoff) {
  Init(true);
  // Use a local/static config.
  base::HistogramTester histogram_tester;
  AddMockFailure();
  AddMockFailure();

  EXPECT_EQ(0, config_client()->failed_attempts_before_success());

  SetDataReductionProxyEnabled(true);
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());

  // First attempt should be unsuccessful.
  config_client()->RetrieveConfig();
  RunUntilIdle();
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  EXPECT_EQ(base::TimeDelta::FromSeconds(20), config_client()->GetDelay());

#if defined(OS_ANDROID)
  EXPECT_FALSE(config_client()->foreground_fetch_pending());
#endif

  // Second attempt should be unsuccessful and backoff time should increase.
  config_client()->RetrieveConfig();
  RunUntilIdle();
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  EXPECT_EQ(base::TimeDelta::FromSeconds(40), config_client()->GetDelay());
  EXPECT_TRUE(persisted_config().empty());

#if defined(OS_ANDROID)
  EXPECT_FALSE(config_client()->foreground_fetch_pending());
#endif

  EXPECT_EQ(2, config_client()->failed_attempts_before_success());
  histogram_tester.ExpectTotalCount(
      "DataReductionProxy.ConfigService.FetchFailedAttemptsBeforeSuccess", 0);
}

// Tests that the config is read successfully on the first attempt.
TEST_F(DataReductionProxyConfigServiceClientTest, RemoteConfigSuccess) {
  Init(true);
  AddMockSuccess();
  SetDataReductionProxyEnabled(true);
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  config_client()->RetrieveConfig();
  RunUntilIdle();
  VerifyRemoteSuccess();
#if defined(OS_ANDROID)
  EXPECT_FALSE(config_client()->foreground_fetch_pending());
#endif
}

// Tests that the config is read successfully on the second attempt.
TEST_F(DataReductionProxyConfigServiceClientTest,
       RemoteConfigSuccessAfterFailure) {
  Init(true);
  base::HistogramTester histogram_tester;

  AddMockFailure();
  AddMockSuccess();

  EXPECT_EQ(0, config_client()->failed_attempts_before_success());

  SetDataReductionProxyEnabled(true);
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());

  // First attempt should be unsuccessful.
  config_client()->RetrieveConfig();
  RunUntilIdle();
  EXPECT_EQ(1, config_client()->failed_attempts_before_success());
  EXPECT_EQ(base::TimeDelta::FromSeconds(20), config_client()->GetDelay());
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  EXPECT_TRUE(request_options()->GetSecureSession().empty());

  // Second attempt should be successful.
  config_client()->RetrieveConfig();
  RunUntilIdle();
  VerifyRemoteSuccess();
  EXPECT_EQ(0, config_client()->failed_attempts_before_success());

  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ConfigService.FetchFailedAttemptsBeforeSuccess", 1,
      1);
}

// Verifies that the config is fetched successfully after IP address changes.
TEST_F(DataReductionProxyConfigServiceClientTest, OnIPAddressChange) {
  Init(true);
  SetDataReductionProxyEnabled(true);
  config_client()->RetrieveConfig();

  const int kFailureCount = 5;

  std::vector<scoped_ptr<net::SocketDataProvider>> socket_data_providers;
  for (int i = 0; i < kFailureCount; ++i) {
    AddMockFailure();
    config_client()->RetrieveConfig();
    RunUntilIdle();
  }

  // Verify that the backoff increased exponentially.
  EXPECT_EQ(base::TimeDelta::FromSeconds(320),
            config_client()->GetDelay());  // 320 = 20 * 2^(5-1)
  EXPECT_EQ(kFailureCount, config_client()->GetBackoffErrorCount());

  // IP address change should reset.
  config_client()->OnIPAddressChanged();
  EXPECT_EQ(0, config_client()->GetBackoffErrorCount());
  EXPECT_TRUE(persisted_config().empty());
  ResetBackoffEntryReleaseTime();

  // Fetching the config should be successful.
  AddMockSuccess();
  config_client()->RetrieveConfig();
  RunUntilIdle();
  VerifyRemoteSuccess();
}

// Verifies that fetching the remote config has no effect if the config client
// is disabled.
TEST_F(DataReductionProxyConfigServiceClientTest, OnIPAddressChangeDisabled) {
  Init(true);
  config_client()->SetEnabled(false);
  SetDataReductionProxyEnabled(true);
  config_client()->RetrieveConfig();
  EXPECT_TRUE(request_options()->GetSecureSession().empty());

  enum : int { kFailureCount = 5 };

  for (int i = 0; i < kFailureCount; ++i) {
    config_client()->RetrieveConfig();
    RunUntilIdle();
    EXPECT_TRUE(request_options()->GetSecureSession().empty());
  }

  EXPECT_EQ(0, config_client()->GetBackoffErrorCount());
  config_client()->OnIPAddressChanged();
  EXPECT_EQ(0, config_client()->GetBackoffErrorCount());

  config_client()->RetrieveConfig();
  RunUntilIdle();

  EXPECT_TRUE(request_options()->GetSecureSession().empty());
}

// Verifies the correctness of AuthFailure when the session key in the request
// headers matches the currrent session key.
TEST_F(DataReductionProxyConfigServiceClientTest, AuthFailure) {
  Init(true);
  net::NetworkChangeNotifier::NotifyObserversOfConnectionTypeChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_WIFI);
  net::HttpRequestHeaders request_headers;
  request_headers.SetHeader(
      "chrome-proxy", "something=something_else, s=" +
                          std::string(kOldSuccessSessionKey) + ", key=value");

  base::HistogramTester histogram_tester;
  AddMockPreviousSuccess();
  AddMockSuccess();
  AddMockPreviousSuccess();

  SetDataReductionProxyEnabled(true);
  histogram_tester.ExpectTotalCount(
      "DataReductionProxy.ConfigService.AuthExpired", 0);
  config_client()->RetrieveConfig();
  RunUntilIdle();
  // First remote config should be fetched.
  VerifyRemoteSuccessWithOldConfig();
  EXPECT_EQ(kOldSuccessSessionKey, request_options()->GetSecureSession());
  EXPECT_EQ(0, config_client()->GetBackoffErrorCount());
  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ConfigService.AuthExpired", false, 1);

  // Trigger an auth failure.
  scoped_refptr<net::HttpResponseHeaders> parsed(new net::HttpResponseHeaders(
      "HTTP/1.1 407 Proxy Authentication Required\n"));
  net::ProxyServer origin = net::ProxyServer::FromURI(
      kOldSuccessOrigin, net::ProxyServer::SCHEME_HTTP);
  // Calling ShouldRetryDueToAuthFailure should trigger fetching of remote
  // config.
  net::LoadTimingInfo load_timing_info;
  load_timing_info.request_start =
      base::TimeTicks::Now() - base::TimeDelta::FromSeconds(1);
  load_timing_info.send_start = load_timing_info.request_start;
  EXPECT_TRUE(config_client()->ShouldRetryDueToAuthFailure(
      request_headers, parsed.get(), origin.host_port_pair(),
      load_timing_info));
  EXPECT_EQ(1, config_client()->GetBackoffErrorCount());
  // Persisted config on pref should be cleared.
  EXPECT_TRUE(persisted_config().empty());
  histogram_tester.ExpectBucketCount(
      "DataReductionProxy.ConfigService.AuthExpired", false, 1);
  histogram_tester.ExpectBucketCount(
      "DataReductionProxy.ConfigService.AuthExpired", true, 1);
  RunUntilIdle();
  histogram_tester.ExpectTotalCount(
      "DataReductionProxy.ConfigService.AuthFailure.LatencyPenalty", 1);

  // Second remote config should be fetched.
  VerifyRemoteSuccess();

  // Trigger a second auth failure.
  origin =
      net::ProxyServer::FromURI(kSuccessOrigin, net::ProxyServer::SCHEME_HTTP);

  EXPECT_EQ(kSuccessSessionKey, request_options()->GetSecureSession());
  request_headers.SetHeader(
      "chrome-proxy", "something=something_else, s=" +
                          std::string(kSuccessSessionKey) + ", key=value");
  // Calling ShouldRetryDueToAuthFailure should trigger fetching of remote
  // config.
  EXPECT_TRUE(config_client()->ShouldRetryDueToAuthFailure(
      request_headers, parsed.get(), origin.host_port_pair(),
      load_timing_info));
  EXPECT_EQ(2, config_client()->GetBackoffErrorCount());
  histogram_tester.ExpectBucketCount(
      "DataReductionProxy.ConfigService.AuthExpired", false, 2);
  histogram_tester.ExpectBucketCount(
      "DataReductionProxy.ConfigService.AuthExpired", true, 2);
  histogram_tester.ExpectTotalCount(
      "DataReductionProxy.ConfigService.AuthFailure.LatencyPenalty", 2);
  RunUntilIdle();
  // Third remote config should be fetched.
  VerifyRemoteSuccessWithOldConfig();

  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ClientConfig.AuthExpiredSessionKey",
      1 /* AUTH_EXPIRED_SESSION_KEY_MATCH */, 2);
}

// Verifies the correctness of AuthFailure when the session key in the request
// headers do not match the currrent session key.
TEST_F(DataReductionProxyConfigServiceClientTest,
       AuthFailureWithRequestHeaders) {
  Init(true);
  net::HttpRequestHeaders request_headers;
  const char kSessionKeyRequestHeaders[] = "123";
  ASSERT_NE(kOldSuccessSessionKey, kSessionKeyRequestHeaders);
  request_headers.SetHeader("chrome-proxy",
                            "s=" + std::string(kSessionKeyRequestHeaders));
  base::HistogramTester histogram_tester;
  AddMockPreviousSuccess();
  AddMockSuccess();
  AddMockPreviousSuccess();

  SetDataReductionProxyEnabled(true);
  histogram_tester.ExpectTotalCount(
      "DataReductionProxy.ConfigService.AuthExpired", 0);
  config_client()->RetrieveConfig();
  RunUntilIdle();
  // First remote config should be fetched.
  VerifyRemoteSuccessWithOldConfig();
  EXPECT_EQ(0, config_client()->GetBackoffErrorCount());
  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ConfigService.AuthExpired", false, 1);

  // Trigger an auth failure.
  scoped_refptr<net::HttpResponseHeaders> parsed(new net::HttpResponseHeaders(
      "HTTP/1.1 407 Proxy Authentication Required\n"));
  net::ProxyServer origin = net::ProxyServer::FromURI(
      kOldSuccessOrigin, net::ProxyServer::SCHEME_HTTP);
  // Calling ShouldRetryDueToAuthFailure should not trigger fetching of remote
  // config since the session key in the request headers do not match the
  // current session key, but the request should be retried.
  net::LoadTimingInfo load_timing_info;
  load_timing_info.request_start =
      base::TimeTicks::Now() - base::TimeDelta::FromSeconds(1);
  load_timing_info.send_start = load_timing_info.request_start;

  EXPECT_TRUE(config_client()->ShouldRetryDueToAuthFailure(
      request_headers, parsed.get(), origin.host_port_pair(),
      load_timing_info));
  EXPECT_EQ(0, config_client()->GetBackoffErrorCount());
  // Persisted config on pref should be cleared.
  EXPECT_FALSE(persisted_config().empty());
  histogram_tester.ExpectBucketCount(
      "DataReductionProxy.ConfigService.AuthExpired", false, 1);
  histogram_tester.ExpectBucketCount(
      "DataReductionProxy.ConfigService.AuthExpired", true, 0);
  RunUntilIdle();
  EXPECT_EQ(kOldSuccessSessionKey, request_options()->GetSecureSession());

  histogram_tester.ExpectUniqueSample(
      "DataReductionProxy.ClientConfig.AuthExpiredSessionKey",
      0 /* AUTH_EXPIRED_SESSION_KEY_MISMATCH */, 1);
}

// Verifies that requests that were not proxied through data saver proxy due to
// missing config are recorded properly.
TEST_F(DataReductionProxyConfigServiceClientTest, HTTPRequests) {
  Init(false);
  const struct {
    std::string url;
    bool enabled_by_user;
    bool expect_histogram;
  } tests[] = {
      {
          // Request should not be logged because data saver is disabled.
          "http://www.example.com/", false, false,
      },
      {
          "http://www.example.com/", true, true,
      },
      {
          "https://www.example.com/", false, false,
      },
      {
          // Request should not be logged because request is HTTPS.
          "https://www.example.com/", true, false,
      },
      {
          // Request to localhost should not be logged.
          "http://127.0.0.1/", true, false,
      },
      {
          // Special use IPv4 address for testing purposes (RFC 5735).
          "http://198.51.100.1/", true, true,
      },
  };

  for (size_t i = 0; i < arraysize(tests); ++i) {
    base::HistogramTester histogram_tester;
    SetDataReductionProxyEnabled(tests[i].enabled_by_user);

    net::TestDelegate test_delegate;

    scoped_ptr<net::URLRequest> request(
        test_url_request_context()->CreateRequest(GURL(tests[i].url), net::IDLE,
                                                  &test_delegate));
    request->Start();
    base::RunLoop().RunUntilIdle();

    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.HTTPRequests",
        tests[i].expect_histogram ? 1 : 0);

    if (tests[i].expect_histogram) {
      histogram_tester.ExpectUniqueSample(
          "DataReductionProxy.ConfigService.HTTPRequests", 0, 1);
    }
  }
}

// Tests that remote config can be applied after the serialized config has been
// applied.
TEST_F(DataReductionProxyConfigServiceClientTest, ApplySerializedConfig) {
  Init(true);
  AddMockSuccess();

  SetDataReductionProxyEnabled(true);
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  config_client()->ApplySerializedConfig(loaded_config());
  VerifySuccessWithLoadedConfig();
  EXPECT_TRUE(persisted_config().empty());

  config_client()->RetrieveConfig();
  RunUntilIdle();
  VerifyRemoteSuccess();
}

// Tests that serialized config has no effect after the config has been
// retrieved successfully.
TEST_F(DataReductionProxyConfigServiceClientTest,
       ApplySerializedConfigAfterReceipt) {
  Init(true);
  AddMockSuccess();

  SetDataReductionProxyEnabled(true);
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  EXPECT_TRUE(request_options()->GetSecureSession().empty());

  // Retrieve the remote config.
  config_client()->RetrieveConfig();
  RunUntilIdle();
  VerifyRemoteSuccess();

  // ApplySerializedConfig should not have any effect since the remote config is
  // already applied.
  config_client()->ApplySerializedConfig(encoded_config());
  VerifyRemoteSuccess();
}

// Tests that a local serialized config can be applied successfully if remote
// config has not been fetched so far.
TEST_F(DataReductionProxyConfigServiceClientTest, ApplySerializedConfigLocal) {
  Init(true);
  SetDataReductionProxyEnabled(true);
  EXPECT_TRUE(configurator()->proxies_for_http().empty());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  EXPECT_TRUE(request_options()->GetSecureSession().empty());

  // ApplySerializedConfig should apply the encoded config.
  config_client()->ApplySerializedConfig(encoded_config());
  EXPECT_EQ(2U, configurator()->proxies_for_http().size());
  EXPECT_TRUE(configurator()->proxies_for_https().empty());
  EXPECT_TRUE(persisted_config().empty());
  EXPECT_FALSE(request_options()->GetSecureSession().empty());
}

#if defined(OS_ANDROID)
// Verifies the correctness of fetching config when Chromium is in background
// and foreground.
TEST_F(DataReductionProxyConfigServiceClientTest, FetchConfigOnForeground) {
  Init(true);
  SetDataReductionProxyEnabled(true);

  {
    // Tests that successful config fetches while Chromium is in background,
    // does not trigger refetches when Chromium comes to foreground.
    base::HistogramTester histogram_tester;
    AddMockSuccess();
    config_client()->set_application_state_background(true);
    config_client()->RetrieveConfig();
    RunUntilIdle();
    VerifyRemoteSuccess();
    EXPECT_FALSE(config_client()->foreground_fetch_pending());
    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.FetchLatency", 1);
    EXPECT_EQ(base::TimeDelta::FromSeconds(kConfigRefreshDurationSeconds),
              config_client()->GetDelay());
    config_client()->set_application_state_background(false);
    config_client()->TriggerApplicationStatusToForeground();
    RunUntilIdle();
    EXPECT_EQ(base::TimeDelta::FromSeconds(kConfigRefreshDurationSeconds),
              config_client()->GetDelay());
    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.FetchLatency", 1);
  }

  {
    // Tests that config fetch failures while Chromium is in foreground does not
    // trigger refetches when Chromium comes to foreground again.
    base::HistogramTester histogram_tester;
    AddMockFailure();
    config_client()->set_application_state_background(false);
    config_client()->RetrieveConfig();
    RunUntilIdle();
    EXPECT_FALSE(config_client()->foreground_fetch_pending());
    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.FetchLatency", 0);
    EXPECT_EQ(base::TimeDelta::FromSeconds(20), config_client()->GetDelay());
    config_client()->TriggerApplicationStatusToForeground();
    RunUntilIdle();
    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.FetchLatency", 0);
    EXPECT_EQ(base::TimeDelta::FromSeconds(20), config_client()->GetDelay());
  }

  {
    // Tests that config fetch failures while Chromium is in background, trigger
    // a refetch when Chromium comes to foreground.
    base::HistogramTester histogram_tester;
    AddMockFailure();
    AddMockSuccess();
    config_client()->set_application_state_background(true);
    config_client()->RetrieveConfig();
    RunUntilIdle();
    EXPECT_TRUE(config_client()->foreground_fetch_pending());
    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.FetchLatency", 0);
    EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxBackgroundFetchIntervalSeconds),
              config_client()->GetDelay());
    config_client()->set_application_state_background(false);
    config_client()->TriggerApplicationStatusToForeground();
    RunUntilIdle();
    EXPECT_FALSE(config_client()->foreground_fetch_pending());
    histogram_tester.ExpectTotalCount(
        "DataReductionProxy.ConfigService.FetchLatency", 1);
    EXPECT_EQ(base::TimeDelta::FromSeconds(kConfigRefreshDurationSeconds),
              config_client()->GetDelay());
    VerifyRemoteSuccess();
  }
}
#endif

}  // namespace data_reduction_proxy
