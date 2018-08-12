// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/ntp_snippets_fetcher.h"

#include "base/json/json_reader.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/histogram_tester.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/ntp_snippets/ntp_snippet.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/signin/core/browser/fake_profile_oauth2_token_service.h"
#include "components/signin/core/browser/fake_signin_manager.h"
#include "components/signin/core/browser/test_signin_client.h"
#include "google_apis/google_api_keys.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ntp_snippets {
namespace {

using testing::ElementsAre;
using testing::Eq;
using testing::IsEmpty;
using testing::IsNull;
using testing::Not;
using testing::NotNull;
using testing::PrintToString;
using testing::SizeIs;
using testing::StartsWith;

const char kTestContentSnippetsServerFormat[] =
    "https://chromereader-pa.googleapis.com/v1/fetch?key=%s";
// Artificial time delay for JSON parsing.
const int64_t kTestJsonParsingLatencyMs = 20;

MATCHER(HasValue, "") {
  return static_cast<bool>(arg);
}

MATCHER_P(PointeeSizeIs,
          size,
          std::string("contains a value with size ") + PrintToString(size)) {
  return arg && static_cast<int>(arg->size()) == size;
}

class MockSnippetsAvailableCallback {
 public:
  // Workaround for gMock's lack of support for movable arguments.
  void WrappedRun(NTPSnippetsFetcher::OptionalSnippets snippets) {
    Run(snippets);
  }

  MOCK_METHOD1(Run, void(const NTPSnippetsFetcher::OptionalSnippets& snippets));
};

// Factory for FakeURLFetcher objects that always generate errors.
class FailingFakeURLFetcherFactory : public net::URLFetcherFactory {
 public:
  std::unique_ptr<net::URLFetcher> CreateURLFetcher(
      int id, const GURL& url, net::URLFetcher::RequestType request_type,
      net::URLFetcherDelegate* d) override {
    return base::WrapUnique(new net::FakeURLFetcher(
        url, d, /*response_data=*/std::string(), net::HTTP_NOT_FOUND,
        net::URLRequestStatus::FAILED));
  }
};

void ParseJson(
    const std::string& json,
    const ntp_snippets::NTPSnippetsFetcher::SuccessCallback& success_callback,
    const ntp_snippets::NTPSnippetsFetcher::ErrorCallback& error_callback) {
  base::JSONReader json_reader;
  std::unique_ptr<base::Value> value = json_reader.ReadToValue(json);
  if (value)
    success_callback.Run(std::move(value));
  else
    error_callback.Run(json_reader.GetErrorMessage());
}

void ParseJsonDelayed(
    const std::string& json,
    const ntp_snippets::NTPSnippetsFetcher::SuccessCallback& success_callback,
    const ntp_snippets::NTPSnippetsFetcher::ErrorCallback& error_callback) {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&ParseJson, json, success_callback, error_callback),
      base::TimeDelta::FromMilliseconds(kTestJsonParsingLatencyMs));
}

class NTPSnippetsFetcherTest : public testing::Test {
 public:
  NTPSnippetsFetcherTest()
      : mock_task_runner_(new base::TestMockTimeTaskRunner()),
        mock_task_runner_handle_(mock_task_runner_),
        signin_client_(new TestSigninClient(nullptr)),
        account_tracker_(new AccountTrackerService()),
        fake_signin_manager_(new FakeSigninManagerBase(signin_client_.get(),
                                                       account_tracker_.get())),
        fake_token_service_(new FakeProfileOAuth2TokenService()),
        snippets_fetcher_(
            fake_signin_manager_.get(),
            fake_token_service_.get(),
            scoped_refptr<net::TestURLRequestContextGetter>(
                new net::TestURLRequestContextGetter(mock_task_runner_.get())),
            base::Bind(&ParseJsonDelayed),
            /*is_stable_channel=*/true),
        test_lang_("en-US"),
        test_url_(base::StringPrintf(kTestContentSnippetsServerFormat,
                                     google_apis::GetAPIKey().c_str())) {
    snippets_fetcher_.SetCallback(
        base::Bind(&MockSnippetsAvailableCallback::WrappedRun,
                   base::Unretained(&mock_callback_)));
    snippets_fetcher_.SetTickClockForTesting(
        mock_task_runner_->GetMockTickClock());
    test_hosts_.insert("www.somehost.com");
    // Increase initial time such that ticks are non-zero.
    mock_task_runner_->FastForwardBy(base::TimeDelta::FromMilliseconds(1234));
  }

  NTPSnippetsFetcher& snippets_fetcher() { return snippets_fetcher_; }
  MockSnippetsAvailableCallback& mock_callback() { return mock_callback_; }
  void FastForwardUntilNoTasksRemain() {
    mock_task_runner_->FastForwardUntilNoTasksRemain();
  }
  const std::string& test_lang() const { return test_lang_; }
  const GURL& test_url() { return test_url_; }
  const std::set<std::string>& test_hosts() const { return test_hosts_; }
  base::HistogramTester& histogram_tester() { return histogram_tester_; }

  void InitFakeURLFetcherFactory() {
    if (fake_url_fetcher_factory_)
      return;
    // Instantiation of factory automatically sets itself as URLFetcher's
    // factory.
    fake_url_fetcher_factory_.reset(new net::FakeURLFetcherFactory(
        /*default_factory=*/&failing_url_fetcher_factory_));
  }

  void SetFakeResponse(const std::string& response_data,
                       net::HttpStatusCode response_code,
                       net::URLRequestStatus::Status status) {
    InitFakeURLFetcherFactory();
    fake_url_fetcher_factory_->SetFakeResponse(test_url_, response_data,
                                               response_code, status);
  }

 private:
  scoped_refptr<base::TestMockTimeTaskRunner> mock_task_runner_;
  base::ThreadTaskRunnerHandle mock_task_runner_handle_;
  FailingFakeURLFetcherFactory failing_url_fetcher_factory_;
  // Initialized lazily in SetFakeResponse().
  std::unique_ptr<net::FakeURLFetcherFactory> fake_url_fetcher_factory_;
  std::unique_ptr<TestSigninClient> signin_client_;
  std::unique_ptr<AccountTrackerService> account_tracker_;
  std::unique_ptr<SigninManagerBase> fake_signin_manager_;
  std::unique_ptr<OAuth2TokenService> fake_token_service_;
  NTPSnippetsFetcher snippets_fetcher_;
  MockSnippetsAvailableCallback mock_callback_;
  const std::string test_lang_;
  const GURL test_url_;
  std::set<std::string> test_hosts_;
  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(NTPSnippetsFetcherTest);
};

TEST_F(NTPSnippetsFetcherTest, ShouldNotFetchOnCreation) {
  // The lack of registered baked in responses would cause any fetch to fail.
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              IsEmpty());
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              IsEmpty());
  EXPECT_THAT(snippets_fetcher().last_status(), IsEmpty());
}

TEST_F(NTPSnippetsFetcherTest, ShouldFetchSuccessfully) {
  const std::string kJsonStr =
      "{\"recos\": [{"
      "  \"contentInfo\": {"
      "    \"url\" : \"http://localhost/foobar\","
      "    \"sourceCorpusInfo\" : [{"
      "      \"ampUrl\" : \"http://localhost/amp\","
      "      \"corpusId\" : \"http://localhost/foobar\","
      "      \"publisherData\": { \"sourceName\" : \"Foo News\" }"
      "    }]"
      "  }"
      "}]}";
  SetFakeResponse(/*data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/PointeeSizeIs(1))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(snippets_fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(NTPSnippetsFetcherTest, ShouldFetchSuccessfullyEmptyList) {
  const std::string kJsonStr = "{\"recos\": []}";
  SetFakeResponse(/*data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/PointeeSizeIs(0))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_status(), Eq("OK"));
  EXPECT_THAT(snippets_fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/0, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
}

TEST_F(NTPSnippetsFetcherTest, ShouldReportEmptyHostsError) {
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(/*hosts=*/std::set<std::string>(),
                                            /*language_code=*/"en-US",
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_status(),
              Eq("Cannot fetch for empty hosts list."));
  EXPECT_THAT(snippets_fetcher().last_json(), IsEmpty());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/1, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              IsEmpty());
  // This particular error gets triggered prior to JSON parsing and hence tests
  // observe no fetch latency.
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/0, /*count=*/1)));
}

TEST_F(NTPSnippetsFetcherTest, ShouldRestrictToHosts) {
  net::TestURLFetcherFactory test_url_fetcher_factory;
  snippets_fetcher().FetchSnippetsFromHosts(
      {"www.somehost1.com", "www.somehost2.com"}, test_lang(), /*count=*/17);
  net::TestURLFetcher* fetcher = test_url_fetcher_factory.GetFetcherByID(0);
  ASSERT_THAT(fetcher, NotNull());
  std::unique_ptr<base::Value> value =
      base::JSONReader::Read(fetcher->upload_data());
  ASSERT_TRUE(value) << " failed to parse JSON: "
                     << PrintToString(fetcher->upload_data());
  const base::DictionaryValue* dict = nullptr;
  ASSERT_TRUE(value->GetAsDictionary(&dict));
  const base::DictionaryValue* local_scoring_params = nullptr;
  ASSERT_TRUE(dict->GetDictionary("advanced_options.local_scoring_params",
                                  &local_scoring_params));
  const base::ListValue* content_selectors = nullptr;
  ASSERT_TRUE(
      local_scoring_params->GetList("content_selectors", &content_selectors));
  ASSERT_THAT(content_selectors->GetSize(), Eq(static_cast<size_t>(2)));
  const base::DictionaryValue* content_selector = nullptr;
  ASSERT_TRUE(content_selectors->GetDictionary(0, &content_selector));
  std::string content_selector_value;
  EXPECT_TRUE(content_selector->GetString("value", &content_selector_value));
  EXPECT_THAT(content_selector_value, Eq("www.somehost1.com"));
  ASSERT_TRUE(content_selectors->GetDictionary(1, &content_selector));
  EXPECT_TRUE(content_selector->GetString("value", &content_selector_value));
  EXPECT_THAT(content_selector_value, Eq("www.somehost2.com"));
}

TEST_F(NTPSnippetsFetcherTest, ShouldReportUrlStatusError) {
  SetFakeResponse(/*data=*/std::string(), net::HTTP_NOT_FOUND,
                  net::URLRequestStatus::FAILED);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_status(),
              Eq("URLRequestStatus error -2"));
  EXPECT_THAT(snippets_fetcher().last_json(), IsEmpty());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/2, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/-2, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              Not(IsEmpty()));
}

TEST_F(NTPSnippetsFetcherTest, ShouldReportHttpError) {
  SetFakeResponse(/*data=*/std::string(), net::HTTP_NOT_FOUND,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_json(), IsEmpty());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/3, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/404, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              Not(IsEmpty()));
}

TEST_F(NTPSnippetsFetcherTest, ShouldReportJsonError) {
  const std::string kInvalidJsonStr = "{ \"recos\": []";
  SetFakeResponse(/*data=*/kInvalidJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_status(),
              StartsWith("Received invalid JSON (error "));
  EXPECT_THAT(snippets_fetcher().last_json(), Eq(kInvalidJsonStr));
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/4, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

TEST_F(NTPSnippetsFetcherTest, ShouldReportJsonErrorForEmptyResponse) {
  SetFakeResponse(/*data=*/std::string(), net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_json(), std::string());
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/4, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
}

TEST_F(NTPSnippetsFetcherTest, ShouldReportInvalidListError) {
  const std::string kJsonStr =
      "{\"recos\": [{ \"contentInfo\": { \"foo\" : \"bar\" }}]}";
  SetFakeResponse(/*data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(snippets_fetcher().last_json(), Eq(kJsonStr));
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/5, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              Not(IsEmpty()));
}

// This test actually verifies that the test setup itself is sane, to prevent
// hard-to-reproduce test failures.
TEST_F(NTPSnippetsFetcherTest, ShouldReportHttpErrorForMissingBakedResponse) {
  InitFakeURLFetcherFactory();
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/Not(HasValue()))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
}

TEST_F(NTPSnippetsFetcherTest, ShouldCancelOngoingFetch) {
  const std::string kJsonStr = "{ \"recos\": [] }";
  SetFakeResponse(/*data=*/kJsonStr, net::HTTP_OK,
                  net::URLRequestStatus::SUCCESS);
  EXPECT_CALL(mock_callback(), Run(/*snippets=*/PointeeSizeIs(0))).Times(1);
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  // Second call to FetchSnippetsFromHosts() overrides/cancels the previous.
  // Callback is expected to be called once.
  snippets_fetcher().FetchSnippetsFromHosts(test_hosts(), test_lang(),
                                            /*count=*/1);
  FastForwardUntilNoTasksRemain();
  EXPECT_THAT(
      histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchResult"),
      ElementsAre(base::Bucket(/*min=*/0, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples(
                  "NewTabPage.Snippets.FetchHttpResponseOrErrorCode"),
              ElementsAre(base::Bucket(/*min=*/200, /*count=*/1)));
  EXPECT_THAT(histogram_tester().GetAllSamples("NewTabPage.Snippets.FetchTime"),
              ElementsAre(base::Bucket(/*min=*/kTestJsonParsingLatencyMs,
                                       /*count=*/1)));
}

}  // namespace
}  // namespace ntp_snippets
