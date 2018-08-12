// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/histogram_tester.h"
#include "base/thread_task_runner_handle.h"
#include "chrome/browser/media/router/issue.h"
#include "chrome/browser/media/router/media_route.h"
#include "chrome/browser/media/router/media_router_metrics.h"
#include "chrome/browser/media/router/media_router_mojo_test.h"
#include "chrome/browser/media/router/media_router_type_converters.h"
#include "chrome/browser/media/router/mock_media_router.h"
#include "chrome/browser/media/router/presentation_session_messages_observer.h"
#include "chrome/browser/media/router/test_helper.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/process_manager_factory.h"
#include "media/base/gmock_callback_support.h"
#include "mojo/message_pump/message_pump_mojo.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::IsEmpty;
using testing::Mock;
using testing::Not;
using testing::Pointee;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace media_router {

namespace {

const char kDescription[] = "description";
const char kError[] = "error";
const char kExtensionId[] = "extension1234";
const char kMessage[] = "message";
const char kSource[] = "source1";
const char kSource2[] = "source2";
const char kRouteId[] = "routeId";
const char kRouteId2[] = "routeId2";
const char kJoinableRouteId[] = "joinableRouteId";
const char kJoinableRouteId2[] = "joinableRouteId2";
const char kSinkId[] = "sink";
const char kSinkId2[] = "sink2";
const char kSinkName[] = "sinkName";
const char kPresentationId[] = "presentationId";
const char kOrigin[] = "http://origin/";
const int kInvalidTabId = -1;
const uint8_t kBinaryMessage[] = {0x01, 0x02, 0x03, 0x04};

bool ArePresentationSessionMessagesEqual(
    const content::PresentationSessionMessage* expected,
    const content::PresentationSessionMessage* actual) {
  if (expected->type != actual->type)
    return false;

  return expected->is_binary() ? *expected->data == *actual->data
                               : expected->message == actual->message;
}

interfaces::IssuePtr CreateMojoIssue(const std::string& title) {
  interfaces::IssuePtr mojoIssue = interfaces::Issue::New();
  mojoIssue->title = title;
  mojoIssue->message = "msg";
  mojoIssue->route_id = "";
  mojoIssue->default_action =
      interfaces::Issue::ActionType::ACTION_TYPE_DISMISS;
  mojoIssue->secondary_actions =
      mojo::Array<interfaces::Issue::ActionType>::New(0);
  mojoIssue->severity = interfaces::Issue::Severity::SEVERITY_WARNING;
  mojoIssue->is_blocking = false;
  mojoIssue->help_url = "";
  return mojoIssue;
}

}  // namespace

class RouteResponseCallbackHandler {
 public:
  MOCK_METHOD3(Invoke,
               void(const MediaRoute* route,
                    const std::string& presentation_id,
                    const std::string& error_text));
};

class SendMessageCallbackHandler {
 public:
  MOCK_METHOD1(Invoke, void(bool));
};

class ListenForMessagesCallbackHandler {
 public:
  ListenForMessagesCallbackHandler(
      ScopedVector<content::PresentationSessionMessage> expected_messages,
      bool pass_ownership)
      : expected_messages_(std::move(expected_messages)),
        pass_ownership_(pass_ownership) {}
  void Invoke(const ScopedVector<content::PresentationSessionMessage>& messages,
              bool pass_ownership) {
    InvokeObserver();
    EXPECT_EQ(pass_ownership_, pass_ownership);
    EXPECT_EQ(messages.size(), expected_messages_.size());
    for (size_t i = 0; i < expected_messages_.size(); ++i) {
      EXPECT_TRUE(ArePresentationSessionMessagesEqual(expected_messages_[i],
                                                      messages[i]));
    }
  }

  MOCK_METHOD0(InvokeObserver, void());

 private:
  ScopedVector<content::PresentationSessionMessage> expected_messages_;
  bool pass_ownership_;
};

template <typename T>
void StoreAndRun(T* result, const base::Closure& closure, const T& result_val) {
  *result = result_val;
  closure.Run();
}

class MediaRouterMojoImplTest : public MediaRouterMojoTest {
 public:
  MediaRouterMojoImplTest() {}
  ~MediaRouterMojoImplTest() override {}
};

// ProcessManager with a mocked method subset, for testing extension suspend
// handling.
class TestProcessManager : public extensions::ProcessManager {
 public:
  explicit TestProcessManager(content::BrowserContext* context)
      : extensions::ProcessManager(
            context,
            context,
            extensions::ExtensionRegistry::Get(context)) {}
  ~TestProcessManager() override {}

  static scoped_ptr<KeyedService> Create(content::BrowserContext* context) {
    return make_scoped_ptr(new TestProcessManager(context));
  }

  MOCK_METHOD1(IsEventPageSuspended, bool(const std::string& ext_id));

  MOCK_METHOD2(WakeEventPage,
               bool(const std::string& extension_id,
                    const base::Callback<void(bool)>& callback));

 private:
  DISALLOW_COPY_AND_ASSIGN(TestProcessManager);
};

// Mockable class for awaiting RegisterMediaRouteProvider callbacks.
class RegisterMediaRouteProviderHandler {
 public:
  MOCK_METHOD1(Invoke, void(const std::string& instance_id));
};

TEST_F(MediaRouterMojoImplTest, CreateRoute) {
  MediaSource media_source(kSource);
  MediaRoute expected_route(kRouteId, media_source,
                            kSinkId, "", false, "", false);
  interfaces::MediaRoutePtr route = interfaces::MediaRoute::New();
  route->media_source = kSource;
  route->media_sink_id = kSinkId;
  route->media_route_id = kRouteId;
  route->description = kDescription;
  route->is_local = true;
  route->for_display = true;

  // Use a lambda function as an invocation target here to work around
  // a limitation with GMock::Invoke that prevents it from using move-only types
  // in runnable parameter lists.
  EXPECT_CALL(mock_media_route_provider_,
              CreateRoute(mojo::String(kSource), mojo::String(kSinkId), _,
                          mojo::String(kOrigin), kInvalidTabId, _))
      .WillOnce(Invoke([&route](
          const mojo::String& source, const mojo::String& sink,
          const mojo::String& presentation_id, const mojo::String& origin,
          int tab_id,
          const interfaces::MediaRouteProvider::CreateRouteCallback& cb) {
        cb.Run(std::move(route), mojo::String());
      }));
  // MediaRouterMojoImpl will start observing local displayable routes as a
  // result of having one created.
  base::RunLoop run_loop;
  EXPECT_CALL(mock_media_route_provider_, StartObservingMediaRoutes(_))
      .WillOnce(Invoke([&run_loop](const mojo::String& source) {
        run_loop.Quit();
      }));

  RouteResponseCallbackHandler handler;
  EXPECT_CALL(handler, Invoke(Pointee(Equals(expected_route)), Not(""), ""));
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &RouteResponseCallbackHandler::Invoke, base::Unretained(&handler)));
  router()->CreateRoute(kSource, kSinkId, GURL(kOrigin), nullptr,
                        route_response_callbacks);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, CreateRouteFails) {
  EXPECT_CALL(mock_media_route_provider_,
              CreateRoute(mojo::String(kSource), mojo::String(kSinkId), _,
                          mojo::String(kOrigin), kInvalidTabId, _))
      .WillOnce(Invoke(
          [](const mojo::String& source, const mojo::String& sink,
             const mojo::String& presentation_id, const mojo::String& origin,
             int tab_id,
             const interfaces::MediaRouteProvider::CreateRouteCallback& cb) {
            cb.Run(interfaces::MediaRoutePtr(), mojo::String(kError));
          }));

  RouteResponseCallbackHandler handler;
  base::RunLoop run_loop;
  EXPECT_CALL(handler, Invoke(nullptr, "", kError))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &RouteResponseCallbackHandler::Invoke, base::Unretained(&handler)));
  router()->CreateRoute(kSource, kSinkId, GURL(kOrigin), nullptr,
                        route_response_callbacks);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, JoinRoute) {
  MediaSource media_source(kSource);
  MediaRoute expected_route(kRouteId, media_source,
                            kSinkId, "", false, "", false);
  interfaces::MediaRoutePtr route = interfaces::MediaRoute::New();
  route->media_source = kSource;
  route->media_sink_id = kSinkId;
  route->media_route_id = kRouteId;
  route->description = kDescription;
  route->is_local = true;
  route->for_display = true;

  // Use a lambda function as an invocation target here to work around
  // a limitation with GMock::Invoke that prevents it from using move-only types
  // in runnable parameter lists.
  EXPECT_CALL(mock_media_route_provider_,
              JoinRoute(mojo::String(kSource), mojo::String(kPresentationId),
                        mojo::String(kOrigin), kInvalidTabId, _))
      .WillOnce(Invoke([&route](
          const mojo::String& source, const mojo::String& presentation_id,
          const mojo::String& origin, int tab_id,
          const interfaces::MediaRouteProvider::JoinRouteCallback& cb) {
        cb.Run(std::move(route), mojo::String());
      }));

  // MediaRouterMojoImpl will start observing local displayable routes as a
  // result of having one created.
  base::RunLoop run_loop;
  EXPECT_CALL(mock_media_route_provider_, StartObservingMediaRoutes(_))
      .WillOnce(Invoke([&run_loop](const mojo::String& source) {
        run_loop.Quit();
      }));

  RouteResponseCallbackHandler handler;
  EXPECT_CALL(handler, Invoke(Pointee(Equals(expected_route)), Not(""), ""));
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &RouteResponseCallbackHandler::Invoke, base::Unretained(&handler)));
  router()->JoinRoute(kSource, kPresentationId, GURL(kOrigin), nullptr,
                      route_response_callbacks);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, JoinRouteFails) {
  EXPECT_CALL(mock_media_route_provider_,
              JoinRoute(mojo::String(kSource), mojo::String(kPresentationId),
                        mojo::String(kOrigin), kInvalidTabId, _))
      .WillOnce(Invoke(
          [](const mojo::String& source, const mojo::String& presentation_id,
             const mojo::String& origin, int tab_id,
             const interfaces::MediaRouteProvider::JoinRouteCallback& cb) {
            cb.Run(interfaces::MediaRoutePtr(), mojo::String(kError));
          }));

  RouteResponseCallbackHandler handler;
  base::RunLoop run_loop;
  EXPECT_CALL(handler, Invoke(nullptr, "", kError))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &RouteResponseCallbackHandler::Invoke, base::Unretained(&handler)));
  router()->JoinRoute(kSource, kPresentationId, GURL(kOrigin), nullptr,
                      route_response_callbacks);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, ConnectRouteByRouteId) {
  MediaSource media_source(kSource);
  MediaRoute expected_route(kRouteId, media_source,
                            kSinkId, "", false, "", false);
  interfaces::MediaRoutePtr route = interfaces::MediaRoute::New();
  route->media_source = kSource;
  route->media_sink_id = kSinkId;
  route->media_route_id = kRouteId;
  route->description = kDescription;
  route->is_local = true;
  route->for_display = true;

  // Use a lambda function as an invocation target here to work around
  // a limitation with GMock::Invoke that prevents it from using move-only types
  // in runnable parameter lists.
  EXPECT_CALL(mock_media_route_provider_,
              ConnectRouteByRouteId(mojo::String(kSource),
                                    mojo::String(kRouteId), _,
                                    mojo::String(kOrigin), kInvalidTabId, _))
      .WillOnce(Invoke([&route](
          const mojo::String& source, const mojo::String& route_id,
          const mojo::String& presentation_id,
          const mojo::String& origin, int tab_id,
          const interfaces::MediaRouteProvider::JoinRouteCallback& cb) {
        cb.Run(std::move(route), mojo::String());
      }));

  // MediaRouterMojoImpl will start observing local displayable routes as a
  // result of having one created.
  base::RunLoop run_loop;
  EXPECT_CALL(mock_media_route_provider_, StartObservingMediaRoutes(_))
      .WillOnce(Invoke([&run_loop](const mojo::String& source) {
        run_loop.Quit();
      }));

  RouteResponseCallbackHandler handler;
  EXPECT_CALL(handler, Invoke(Pointee(Equals(expected_route)), Not(""), ""));
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &RouteResponseCallbackHandler::Invoke, base::Unretained(&handler)));
  router()->ConnectRouteByRouteId(kSource, kRouteId, GURL(kOrigin), nullptr,
                                  route_response_callbacks);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, ConnectRouteByRouteIdFails) {
  EXPECT_CALL(mock_media_route_provider_,
              ConnectRouteByRouteId(mojo::String(kSource),
                                    mojo::String(kRouteId), _,
                                    mojo::String(kOrigin), kInvalidTabId, _))
      .WillOnce(Invoke(
          [](const mojo::String& source,
             const mojo::String& route_id,
             const mojo::String& presentation_id,
             const mojo::String& origin, int tab_id,
             const interfaces::MediaRouteProvider::JoinRouteCallback& cb) {
            cb.Run(interfaces::MediaRoutePtr(), mojo::String(kError));
          }));

  RouteResponseCallbackHandler handler;
  base::RunLoop run_loop;
  EXPECT_CALL(handler, Invoke(nullptr, "", kError))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &RouteResponseCallbackHandler::Invoke, base::Unretained(&handler)));
  router()->ConnectRouteByRouteId(kSource, kRouteId, GURL(kOrigin), nullptr,
                                  route_response_callbacks);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, DetachRoute) {
  base::RunLoop run_loop;
  EXPECT_CALL(mock_media_route_provider_, DetachRoute(mojo::String(kRouteId)))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  router()->DetachRoute(kRouteId);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, TerminateRoute) {
  base::RunLoop run_loop;
  EXPECT_CALL(mock_media_route_provider_,
              TerminateRoute(mojo::String(kRouteId)))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  router()->TerminateRoute(kRouteId);
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, HandleIssue) {
  MockIssuesObserver issue_observer1(router());
  MockIssuesObserver issue_observer2(router());
  issue_observer1.RegisterObserver();
  issue_observer2.RegisterObserver();

  interfaces::IssuePtr mojo_issue1 = CreateMojoIssue("title 1");
  const Issue& expected_issue1 = mojo_issue1.To<Issue>();

  const Issue* issue;
  EXPECT_CALL(issue_observer1,
              OnIssueUpdated(Pointee(EqualsIssue(expected_issue1))))
      .WillOnce(SaveArg<0>(&issue));
  base::RunLoop run_loop;
  EXPECT_CALL(issue_observer2,
              OnIssueUpdated(Pointee(EqualsIssue(expected_issue1))))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  media_router_proxy_->OnIssue(std::move(mojo_issue1));
  run_loop.Run();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&issue_observer1));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&issue_observer2));

  EXPECT_CALL(issue_observer1, OnIssueUpdated(nullptr));
  EXPECT_CALL(issue_observer2, OnIssueUpdated(nullptr));

  router()->ClearIssue(issue->id());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&issue_observer1));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&issue_observer2));
  router()->UnregisterIssuesObserver(&issue_observer1);
  interfaces::IssuePtr mojo_issue2 = CreateMojoIssue("title 2");
  const Issue& expected_issue2 = mojo_issue2.To<Issue>();

  EXPECT_CALL(issue_observer2,
              OnIssueUpdated(Pointee(EqualsIssue(expected_issue2))));
  router()->AddIssue(expected_issue2);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&issue_observer2));

  EXPECT_CALL(issue_observer2, OnIssueUpdated(nullptr));
  router()->ClearIssue(issue->id());
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&issue_observer2));

  base::RunLoop run_loop2;
  EXPECT_CALL(issue_observer2,
              OnIssueUpdated(Pointee(EqualsIssue(expected_issue2))))
      .WillOnce(InvokeWithoutArgs([&run_loop2]() {
        run_loop2.Quit();
      }));
  media_router_proxy_->OnIssue(std::move(mojo_issue2));
  run_loop2.Run();

  issue_observer1.UnregisterObserver();
  issue_observer2.UnregisterObserver();
}

TEST_F(MediaRouterMojoImplTest, RegisterAndUnregisterMediaSinksObserver) {
  router()->OnSinkAvailabilityUpdated(
      interfaces::MediaRouter::SINK_AVAILABILITY_AVAILABLE);
  MediaSource media_source(kSource);

  // These should only be called once even if there is more than one observer
  // for a given source.
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource)));
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource2)));

  scoped_ptr<MockMediaSinksObserver> sinks_observer(
      new MockMediaSinksObserver(router(), media_source));
  EXPECT_TRUE(sinks_observer->Init());
  scoped_ptr<MockMediaSinksObserver> extra_sinks_observer(
      new MockMediaSinksObserver(router(), media_source));
  EXPECT_TRUE(extra_sinks_observer->Init());
  scoped_ptr<MockMediaSinksObserver> unrelated_sinks_observer(
      new MockMediaSinksObserver(router(), MediaSource(kSource2)));
  EXPECT_TRUE(unrelated_sinks_observer->Init());
  ProcessEventLoop();

  std::vector<MediaSink> expected_sinks;
  expected_sinks.push_back(
      MediaSink(kSinkId, kSinkName, MediaSink::IconType::CAST));
  expected_sinks.push_back(
      MediaSink(kSinkId2, kSinkName, MediaSink::IconType::CAST));

  mojo::Array<interfaces::MediaSinkPtr> mojo_sinks(2);
  mojo_sinks[0] = interfaces::MediaSink::New();
  mojo_sinks[0]->sink_id = kSinkId;
  mojo_sinks[0]->name = kSinkName;
  mojo_sinks[0]->icon_type =
      media_router::interfaces::MediaSink::IconType::ICON_TYPE_CAST;
  mojo_sinks[1] = interfaces::MediaSink::New();
  mojo_sinks[1]->sink_id = kSinkId2;
  mojo_sinks[1]->name = kSinkName;
  mojo_sinks[1]->icon_type =
      media_router::interfaces::MediaSink::IconType::ICON_TYPE_CAST;

  base::RunLoop run_loop;
  EXPECT_CALL(*sinks_observer, OnSinksReceived(SequenceEquals(expected_sinks)));
  EXPECT_CALL(*extra_sinks_observer,
              OnSinksReceived(SequenceEquals(expected_sinks)))
      .WillOnce(InvokeWithoutArgs([&run_loop]() { run_loop.Quit(); }));
  media_router_proxy_->OnSinksReceived(media_source.id(),
                                       std::move(mojo_sinks));
  run_loop.Run();

  // Since the MediaRouterMojoImpl has already received results for
  // |media_source|, return cached results to observers that are subsequently
  // registered.
  scoped_ptr<MockMediaSinksObserver> cached_sinks_observer(
      new MockMediaSinksObserver(router(), media_source));
  EXPECT_CALL(*cached_sinks_observer,
              OnSinksReceived(SequenceEquals(expected_sinks)));
  EXPECT_TRUE(cached_sinks_observer->Init());

  base::RunLoop run_loop2;
  EXPECT_CALL(mock_media_route_provider_,
              StopObservingMediaSinks(mojo::String(kSource)));
  EXPECT_CALL(mock_media_route_provider_,
              StopObservingMediaSinks(mojo::String(kSource2)))
      .WillOnce(InvokeWithoutArgs([&run_loop2]() {
        run_loop2.Quit();
      }));
  sinks_observer.reset();
  extra_sinks_observer.reset();
  unrelated_sinks_observer.reset();
  cached_sinks_observer.reset();
  run_loop2.Run();
}

TEST_F(MediaRouterMojoImplTest,
       RegisterMediaSinksObserverWithAvailabilityChange) {
  // When availability is UNAVAILABLE, no calls should be made to MRPM.
  router()->OnSinkAvailabilityUpdated(
      interfaces::MediaRouter::SINK_AVAILABILITY_UNAVAILABLE);
  MediaSource media_source(kSource);
  scoped_ptr<MockMediaSinksObserver> sinks_observer(
      new MockMediaSinksObserver(router(), media_source));
  EXPECT_CALL(*sinks_observer, OnSinksReceived(IsEmpty()));
  EXPECT_TRUE(sinks_observer->Init());
  MediaSource media_source2(kSource2);
  scoped_ptr<MockMediaSinksObserver> sinks_observer2(
      new MockMediaSinksObserver(router(), media_source2));
  EXPECT_CALL(*sinks_observer2, OnSinksReceived(IsEmpty()));
  EXPECT_TRUE(sinks_observer2->Init());
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource)))
      .Times(0);
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource2)))
      .Times(0);
  ProcessEventLoop();
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_media_route_provider_));

  // When availability transitions AVAILABLE, existing sink queries should be
  // sent to MRPM.
  router()->OnSinkAvailabilityUpdated(
      interfaces::MediaRouter::SINK_AVAILABILITY_AVAILABLE);
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource)))
      .Times(1);
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource2)))
      .Times(1);
  ProcessEventLoop();
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_media_route_provider_));

  // No change in availability status; no calls should be made to MRPM.
  router()->OnSinkAvailabilityUpdated(
      interfaces::MediaRouter::SINK_AVAILABILITY_AVAILABLE);
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource)))
      .Times(0);
  EXPECT_CALL(mock_media_route_provider_,
              StartObservingMediaSinks(mojo::String(kSource2)))
      .Times(0);
  ProcessEventLoop();
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_media_route_provider_));

  // When availability is UNAVAILABLE, queries are already removed from MRPM.
  // Unregistering observer won't result in call to MRPM to remove query.
  router()->OnSinkAvailabilityUpdated(
      interfaces::MediaRouter::SINK_AVAILABILITY_UNAVAILABLE);
  EXPECT_CALL(mock_media_route_provider_,
              StopObservingMediaSinks(mojo::String(kSource)))
      .Times(0);
  sinks_observer.reset();
  ProcessEventLoop();
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_media_route_provider_));

  // When availability is AVAILABLE, call is made to MRPM to remove query when
  // observer is unregistered.
  router()->OnSinkAvailabilityUpdated(
      interfaces::MediaRouter::SINK_AVAILABILITY_AVAILABLE);
  EXPECT_CALL(mock_media_route_provider_,
              StopObservingMediaSinks(mojo::String(kSource2)));
  sinks_observer2.reset();
  ProcessEventLoop();
}

TEST_F(MediaRouterMojoImplTest, RegisterAndUnregisterMediaRoutesObserver) {
  MockMediaRouter mock_router;
  MediaSource media_source(kSource);
  MediaSource different_media_source(kSource2);
  EXPECT_CALL(mock_media_route_provider_,
      StartObservingMediaRoutes(mojo::String(media_source.id()))).Times(2);
  EXPECT_CALL(mock_media_route_provider_,
      StartObservingMediaRoutes(
          mojo::String(different_media_source.id()))).Times(1);

  MediaRoutesObserver* observer_captured;
  EXPECT_CALL(mock_router, RegisterMediaRoutesObserver(_))
      .Times(3)
      .WillRepeatedly(SaveArg<0>(&observer_captured));
  MockMediaRoutesObserver routes_observer(&mock_router, media_source.id());
  EXPECT_EQ(observer_captured, &routes_observer);
  MockMediaRoutesObserver extra_routes_observer(&mock_router,
      media_source.id());
  EXPECT_EQ(observer_captured, &extra_routes_observer);
  MockMediaRoutesObserver different_routes_observer(&mock_router,
      different_media_source.id());
  EXPECT_EQ(observer_captured, &different_routes_observer);
  router()->RegisterMediaRoutesObserver(&routes_observer);
  router()->RegisterMediaRoutesObserver(&extra_routes_observer);
  router()->RegisterMediaRoutesObserver(&different_routes_observer);

  std::vector<MediaRoute> expected_routes;
  expected_routes.push_back(MediaRoute(kRouteId, media_source, kSinkId,
                                       kDescription, false, "", false));
  expected_routes.push_back(MediaRoute(kRouteId2, media_source, kSinkId,
                                       kDescription, false, "", false));
  std::vector<MediaRoute::Id> expected_joinable_route_ids;
  expected_joinable_route_ids.push_back(kJoinableRouteId);
  expected_joinable_route_ids.push_back(kJoinableRouteId2);

  mojo::Array<mojo::String> mojo_joinable_routes(2);
  mojo_joinable_routes[0] = kJoinableRouteId;
  mojo_joinable_routes[1] = kJoinableRouteId2;

  mojo::Array<interfaces::MediaRoutePtr> mojo_routes(2);
  mojo_routes[0] = interfaces::MediaRoute::New();
  mojo_routes[0]->media_route_id = kRouteId;
  mojo_routes[0]->media_source = kSource;
  mojo_routes[0]->media_sink_id = kSinkId;
  mojo_routes[0]->description = kDescription;
  mojo_routes[0]->is_local = false;
  mojo_routes[1] = interfaces::MediaRoute::New();
  mojo_routes[1]->media_route_id = kRouteId2;
  mojo_routes[1]->media_source = kSource;
  mojo_routes[1]->media_sink_id = kSinkId;
  mojo_routes[1]->description = kDescription;
  mojo_routes[1]->is_local = false;

  EXPECT_CALL(routes_observer,
              OnRoutesUpdated(SequenceEquals(expected_routes),
                              expected_joinable_route_ids));
  EXPECT_CALL(extra_routes_observer,
              OnRoutesUpdated(SequenceEquals(expected_routes),
                              expected_joinable_route_ids));
  EXPECT_CALL(different_routes_observer,
              OnRoutesUpdated(SequenceEquals(expected_routes),
                              expected_joinable_route_ids)).Times(0);
  media_router_proxy_->OnRoutesUpdated(std::move(mojo_routes),
                                       media_source.id(),
                                       std::move(mojo_joinable_routes));
  ProcessEventLoop();

  EXPECT_CALL(mock_router, UnregisterMediaRoutesObserver(&routes_observer));
  EXPECT_CALL(mock_router,
              UnregisterMediaRoutesObserver(&extra_routes_observer));
  EXPECT_CALL(mock_router,
              UnregisterMediaRoutesObserver(&different_routes_observer));
  router()->UnregisterMediaRoutesObserver(&routes_observer);
  router()->UnregisterMediaRoutesObserver(&extra_routes_observer);
  router()->UnregisterMediaRoutesObserver(&different_routes_observer);
  EXPECT_CALL(mock_media_route_provider_,
              StopObservingMediaRoutes(
                  mojo::String(media_source.id()))).Times(1);
  EXPECT_CALL(mock_media_route_provider_,
              StopObservingMediaRoutes(
                  mojo::String(different_media_source.id())));
  ProcessEventLoop();
}

TEST_F(MediaRouterMojoImplTest, SendRouteMessage) {
  EXPECT_CALL(
      mock_media_route_provider_,
      SendRouteMessage(mojo::String(kRouteId), mojo::String(kMessage), _))
      .WillOnce(Invoke([](
          const MediaRoute::Id& route_id, const std::string& message,
          const interfaces::MediaRouteProvider::SendRouteMessageCallback& cb) {
        cb.Run(true);
      }));

  base::RunLoop run_loop;
  SendMessageCallbackHandler handler;
  EXPECT_CALL(handler, Invoke(true))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  router()->SendRouteMessage(kRouteId, kMessage,
                             base::Bind(&SendMessageCallbackHandler::Invoke,
                                        base::Unretained(&handler)));
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, SendRouteBinaryMessage) {
  scoped_ptr<std::vector<uint8_t>> expected_binary_data(
      new std::vector<uint8_t>(kBinaryMessage,
                               kBinaryMessage + arraysize(kBinaryMessage)));

  EXPECT_CALL(mock_media_route_provider_,
              SendRouteBinaryMessageInternal(mojo::String(kRouteId), _, _))
      .WillOnce(Invoke([](
          const MediaRoute::Id& route_id, const std::vector<uint8_t>& data,
          const interfaces::MediaRouteProvider::SendRouteMessageCallback& cb) {
        EXPECT_EQ(
            0, memcmp(kBinaryMessage, &(data[0]), arraysize(kBinaryMessage)));
        cb.Run(true);
      }));

  base::RunLoop run_loop;
  SendMessageCallbackHandler handler;
  EXPECT_CALL(handler, Invoke(true))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
        run_loop.Quit();
      }));
  router()->SendRouteBinaryMessage(
      kRouteId, std::move(expected_binary_data),
      base::Bind(&SendMessageCallbackHandler::Invoke,
                 base::Unretained(&handler)));
  run_loop.Run();
}

TEST_F(MediaRouterMojoImplTest, PresentationSessionMessagesSingleObserver) {
  mojo::Array<interfaces::RouteMessagePtr> mojo_messages(2);
  mojo_messages[0] = interfaces::RouteMessage::New();
  mojo_messages[0]->type = interfaces::RouteMessage::Type::TYPE_TEXT;
  mojo_messages[0]->message = "text";
  mojo_messages[1] = interfaces::RouteMessage::New();
  mojo_messages[1]->type = interfaces::RouteMessage::Type::TYPE_BINARY;
  mojo_messages[1]->data.push_back(1);

  ScopedVector<content::PresentationSessionMessage> expected_messages;
  scoped_ptr<content::PresentationSessionMessage> message;
  message.reset(new content::PresentationSessionMessage(
      content::PresentationMessageType::TEXT));
  message->message = "text";
  expected_messages.push_back(std::move(message));

  message.reset(new content::PresentationSessionMessage(
      content::PresentationMessageType::ARRAY_BUFFER));
  message->data.reset(new std::vector<uint8_t>(1, 1));
  expected_messages.push_back(std::move(message));

  base::RunLoop run_loop;
  MediaRoute::Id expected_route_id("foo");
  interfaces::MediaRouteProvider::ListenForRouteMessagesCallback mojo_callback;
  EXPECT_CALL(mock_media_route_provider_,
              ListenForRouteMessages(Eq(expected_route_id), _))
      .WillOnce(DoAll(SaveArg<1>(&mojo_callback),
                      InvokeWithoutArgs([&run_loop]() {
                        run_loop.Quit();
                      })));

  // |pass_ownership| param is "true" here because there is only one observer.
  ListenForMessagesCallbackHandler handler(std::move(expected_messages), true);

  EXPECT_CALL(handler, InvokeObserver());
  // Creating PresentationSessionMessagesObserver will register itself to the
  // MediaRouter, which in turn will start listening for route messages.
  scoped_ptr<PresentationSessionMessagesObserver> observer(
      new PresentationSessionMessagesObserver(
          base::Bind(&ListenForMessagesCallbackHandler::Invoke,
                     base::Unretained(&handler)),
          expected_route_id, router()));
  run_loop.Run();

  base::RunLoop run_loop2;
  // Simulate messages by invoking the saved mojo callback.
  // We expect one more ListenForRouteMessages call since |observer| was
  // still registered when the first set of messages arrived.
  mojo_callback.Run(std::move(mojo_messages), false);
  interfaces::MediaRouteProvider::ListenForRouteMessagesCallback
      mojo_callback_2;
  EXPECT_CALL(mock_media_route_provider_, ListenForRouteMessages(_, _))
      .WillOnce(DoAll(SaveArg<1>(&mojo_callback_2),
                      InvokeWithoutArgs([&run_loop2]() {
                        run_loop2.Quit();
                      })));
  run_loop2.Run();

  base::RunLoop run_loop3;
  // Stop listening for messages. In particular, MediaRouterMojoImpl will not
  // call ListenForRouteMessages again when it sees there are no more observers.
  mojo::Array<interfaces::RouteMessagePtr> mojo_messages_2(1);
  mojo_messages_2[0] = interfaces::RouteMessage::New();
  mojo_messages_2[0]->type = interfaces::RouteMessage::Type::TYPE_TEXT;
  mojo_messages_2[0]->message = "foo";
  observer.reset();
  mojo_callback_2.Run(std::move(mojo_messages_2), false);
  EXPECT_CALL(mock_media_route_provider_, StopListeningForRouteMessages(_))
      .WillOnce(InvokeWithoutArgs([&run_loop3]() {
        run_loop3.Quit();
      }));
  run_loop3.Run();
}

TEST_F(MediaRouterMojoImplTest, PresentationSessionMessagesMultipleObservers) {
  mojo::Array<interfaces::RouteMessagePtr> mojo_messages(2);
  mojo_messages[0] = interfaces::RouteMessage::New();
  mojo_messages[0]->type = interfaces::RouteMessage::Type::TYPE_TEXT;
  mojo_messages[0]->message = "text";
  mojo_messages[1] = interfaces::RouteMessage::New();
  mojo_messages[1]->type = interfaces::RouteMessage::Type::TYPE_BINARY;
  mojo_messages[1]->data.push_back(1);

  ScopedVector<content::PresentationSessionMessage> expected_messages;
  scoped_ptr<content::PresentationSessionMessage> message;
  message.reset(new content::PresentationSessionMessage(
      content::PresentationMessageType::TEXT));
  message->message = "text";
  expected_messages.push_back(std::move(message));

  message.reset(new content::PresentationSessionMessage(
      content::PresentationMessageType::ARRAY_BUFFER));
  message->data.reset(new std::vector<uint8_t>(1, 1));
  expected_messages.push_back(std::move(message));

  base::RunLoop run_loop;
  MediaRoute::Id expected_route_id("foo");
  interfaces::MediaRouteProvider::ListenForRouteMessagesCallback mojo_callback;
  EXPECT_CALL(mock_media_route_provider_,
              ListenForRouteMessages(Eq(expected_route_id), _))
      .WillOnce(DoAll(SaveArg<1>(&mojo_callback),
                      InvokeWithoutArgs([&run_loop]() {
                        run_loop.Quit();
                      })));

  // |pass_ownership| param is "false" here because there are more than one
  // observers.
  ListenForMessagesCallbackHandler handler(std::move(expected_messages), false);

  EXPECT_CALL(handler, InvokeObserver()).Times(2);
  // Creating PresentationSessionMessagesObserver will register itself to the
  // MediaRouter, which in turn will start listening for route messages.
  scoped_ptr<PresentationSessionMessagesObserver> observer1(
      new PresentationSessionMessagesObserver(
          base::Bind(&ListenForMessagesCallbackHandler::Invoke,
                     base::Unretained(&handler)),
          expected_route_id, router()));
  scoped_ptr<PresentationSessionMessagesObserver> observer2(
      new PresentationSessionMessagesObserver(
          base::Bind(&ListenForMessagesCallbackHandler::Invoke,
                     base::Unretained(&handler)),
          expected_route_id, router()));
  run_loop.Run();

  base::RunLoop run_loop2;
  // Simulate messages by invoking the saved mojo callback.
  // We expect one more ListenForRouteMessages call since |observer| was
  // still registered when the first set of messages arrived.
  mojo_callback.Run(std::move(mojo_messages), false);
  interfaces::MediaRouteProvider::ListenForRouteMessagesCallback
      mojo_callback_2;
  EXPECT_CALL(mock_media_route_provider_, ListenForRouteMessages(_, _))
      .WillOnce(DoAll(SaveArg<1>(&mojo_callback_2),
                      InvokeWithoutArgs([&run_loop2]() {
                        run_loop2.Quit();
                      })));
  run_loop2.Run();

  base::RunLoop run_loop3;
  // Stop listening for messages. In particular, MediaRouterMojoImpl will not
  // call ListenForRouteMessages again when it sees there are no more observers.
  mojo::Array<interfaces::RouteMessagePtr> mojo_messages_2(1);
  mojo_messages_2[0] = interfaces::RouteMessage::New();
  mojo_messages_2[0]->type = interfaces::RouteMessage::Type::TYPE_TEXT;
  mojo_messages_2[0]->message = "foo";
  observer1.reset();
  observer2.reset();
  mojo_callback_2.Run(std::move(mojo_messages_2), false);
  EXPECT_CALL(mock_media_route_provider_, StopListeningForRouteMessages(_))
      .WillOnce(InvokeWithoutArgs([&run_loop3]() {
        run_loop3.Quit();
      }));
  run_loop3.Run();
}

TEST_F(MediaRouterMojoImplTest, PresentationSessionMessagesError) {
  MediaRoute::Id expected_route_id("foo");
  interfaces::MediaRouteProvider::ListenForRouteMessagesCallback mojo_callback;
  base::RunLoop run_loop;
  EXPECT_CALL(mock_media_route_provider_,
              ListenForRouteMessages(Eq(expected_route_id), _))
      .WillOnce(DoAll(SaveArg<1>(&mojo_callback),
                      InvokeWithoutArgs([&run_loop]() {
                        run_loop.Quit();
                      })));

  ListenForMessagesCallbackHandler handler(
      ScopedVector<content::PresentationSessionMessage>(), true);

  // Creating PresentationSessionMessagesObserver will register itself to the
  // MediaRouter, which in turn will start listening for route messages.
  scoped_ptr<PresentationSessionMessagesObserver> observer1(
      new PresentationSessionMessagesObserver(
          base::Bind(&ListenForMessagesCallbackHandler::Invoke,
                     base::Unretained(&handler)),
          expected_route_id, router()));
  run_loop.Run();

  mojo_callback.Run(mojo::Array<interfaces::RouteMessagePtr>(0), true);
  ProcessEventLoop();
}

TEST_F(MediaRouterMojoImplTest, PresentationConnectionStateChangedCallback) {
  using PresentationConnectionState =
      interfaces::MediaRouter::PresentationConnectionState;

  MediaRoute::Id route_id("route-id");
  const std::string kPresentationUrl("http://foo.fakeUrl");
  const std::string kPresentationId("pid");
  content::PresentationSessionInfo connection(kPresentationUrl,
                                              kPresentationId);
  MockPresentationConnectionStateChangedCallback callback;
  scoped_ptr<PresentationConnectionStateSubscription> subscription =
      router()->AddPresentationConnectionStateChangedCallback(
          route_id,
          base::Bind(&MockPresentationConnectionStateChangedCallback::Run,
                     base::Unretained(&callback)));

  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(content::PRESENTATION_CONNECTION_STATE_CLOSED))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
                  run_loop.Quit();
                }));
  media_router_proxy_->OnPresentationConnectionStateChanged(
      route_id,
      PresentationConnectionState::PRESENTATION_CONNECTION_STATE_CLOSED);
  run_loop.Run();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&callback));

  base::RunLoop run_loop2;
  // Right now we don't keep track of previous state so the callback will be
  // invoked with the same state again.
  EXPECT_CALL(callback, Run(content::PRESENTATION_CONNECTION_STATE_CLOSED))
      .WillOnce(InvokeWithoutArgs([&run_loop2]() {
                  run_loop2.Quit();
                }));
  media_router_proxy_->OnPresentationConnectionStateChanged(
      route_id,
      PresentationConnectionState::PRESENTATION_CONNECTION_STATE_CLOSED);
  run_loop2.Run();

  // Callback has been removed, so we don't expect it to be called anymore.
  subscription.reset();
  EXPECT_TRUE(router()->presentation_connection_state_callbacks_.empty());

  EXPECT_CALL(callback, Run(content::PRESENTATION_CONNECTION_STATE_CLOSED))
      .Times(0);
  media_router_proxy_->OnPresentationConnectionStateChanged(
      route_id,
      PresentationConnectionState::PRESENTATION_CONNECTION_STATE_CLOSED);
  ProcessEventLoop();
}

TEST_F(MediaRouterMojoImplTest, HasLocalRoute) {
  EXPECT_FALSE(router()->HasLocalDisplayRoute());
  interfaces::MediaRoutePtr mojo_route1 = interfaces::MediaRoute::New();
  mojo_route1->media_route_id = "routeId1";
  mojo_route1->media_sink_id = "sinkId";
  mojo_route1->is_local = false;
  mojo_route1->for_display = false;
  router()->RouteResponseReceived("presentationId1",
                                  std::vector<MediaRouteResponseCallback>(),
                                  std::move(mojo_route1), "");
  EXPECT_FALSE(router()->HasLocalDisplayRoute());

  interfaces::MediaRoutePtr mojo_route2 = interfaces::MediaRoute::New();
  mojo_route2->media_route_id = "routeId2";
  mojo_route2->media_sink_id = "sinkId";
  mojo_route2->is_local = false;
  mojo_route2->for_display = true;
  router()->RouteResponseReceived("presentationId2",
                                  std::vector<MediaRouteResponseCallback>(),
                                  std::move(mojo_route2), "");
  EXPECT_FALSE(router()->HasLocalDisplayRoute());

  interfaces::MediaRoutePtr mojo_route3 = interfaces::MediaRoute::New();
  mojo_route3->media_route_id = "routeId3";
  mojo_route3->media_sink_id = "sinkId";
  mojo_route3->is_local = true;
  mojo_route3->for_display = false;
  router()->RouteResponseReceived("presentationId3",
                                  std::vector<MediaRouteResponseCallback>(),
                                  std::move(mojo_route3), "");
  EXPECT_FALSE(router()->HasLocalDisplayRoute());

  interfaces::MediaRoutePtr mojo_route4 = interfaces::MediaRoute::New();
  mojo_route4->media_route_id = "routeId4";
  mojo_route4->media_sink_id = "sinkId";
  mojo_route4->is_local = true;
  mojo_route4->for_display = true;
  router()->RouteResponseReceived("presentationId4",
                                  std::vector<MediaRouteResponseCallback>(),
                                  std::move(mojo_route4), "");
  EXPECT_TRUE(router()->HasLocalDisplayRoute());
}

TEST_F(MediaRouterMojoImplTest, QueuedWhileAsleep) {
  base::RunLoop run_loop;
  EXPECT_CALL(mock_event_page_tracker_, IsEventPageSuspended(extension_id()))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_event_page_tracker_, WakeEventPage(extension_id(), _))
      .Times(2)
      .WillOnce(Return(true))
      .WillOnce(DoAll(InvokeWithoutArgs([&run_loop]() {
                        run_loop.Quit();
                      }), Return(true)));
  router()->DetachRoute(kRouteId);
  router()->DetachRoute(kRouteId2);
  run_loop.Run();
  EXPECT_CALL(mock_event_page_tracker_, IsEventPageSuspended(extension_id()))
      .Times(1)
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_media_route_provider_, DetachRoute(mojo::String(kRouteId)));
  EXPECT_CALL(mock_media_route_provider_, DetachRoute(mojo::String(kRouteId2)));
  ConnectProviderManagerService();
  ProcessEventLoop();
}

class MediaRouterMojoExtensionTest : public ::testing::Test {
 public:
  MediaRouterMojoExtensionTest()
    : process_manager_(nullptr),
      message_loop_(mojo::common::MessagePumpMojo::Create())
  {}

  ~MediaRouterMojoExtensionTest() override {}

 protected:
  void SetUp() override {
    profile_.reset(new TestingProfile);
    // Set up a mock ProcessManager instance.
    extensions::ProcessManagerFactory::GetInstance()->SetTestingFactory(
        profile_.get(), &TestProcessManager::Create);
    process_manager_ = static_cast<TestProcessManager*>(
        extensions::ProcessManager::Get(profile_.get()));
    DCHECK(process_manager_);

    // Create MR and its proxy, so that it can be accessed through Mojo.
    media_router_.reset(new MediaRouterMojoImpl(process_manager_));
    ProcessEventLoop();
  }

  void TearDown() override {
    media_router_.reset();
    profile_.reset();
    // Explicitly delete the TestingBrowserProcess before |message_loop_|.
    // This allows it to do cleanup before |message_loop_| goes away.
    TestingBrowserProcess::DeleteInstance();
  }

  // Constructs bindings so that |media_router_| delegates calls to
  // |mojo_media_router_|, which are then handled by
  // |mock_media_route_provider_service_|.
  void BindMediaRouteProvider() {
    binding_.reset(new mojo::Binding<interfaces::MediaRouteProvider>(
        &mock_media_route_provider_,
        mojo::GetProxy(&media_route_provider_proxy_)));
    media_router_->BindToMojoRequest(mojo::GetProxy(&media_router_proxy_),
                                     kExtensionId);
  }

  void ResetMediaRouteProvider() {
    binding_.reset();
    media_router_->BindToMojoRequest(mojo::GetProxy(&media_router_proxy_),
                                     kExtensionId);
  }

  void RegisterMediaRouteProvider() {
    media_router_proxy_->RegisterMediaRouteProvider(
        std::move(media_route_provider_proxy_),
        base::Bind(&RegisterMediaRouteProviderHandler::Invoke,
                   base::Unretained(&provide_handler_)));
  }

  void ProcessEventLoop() {
    message_loop_.RunUntilIdle();
  }

  void ExpectWakeReasonBucketCount(MediaRouteProviderWakeReason reason,
                                   int expected_count) {
    histogram_tester_.ExpectBucketCount("MediaRouter.Provider.WakeReason",
                                        static_cast<int>(reason),
                                        expected_count);
  }

  scoped_ptr<MediaRouterMojoImpl> media_router_;
  RegisterMediaRouteProviderHandler provide_handler_;
  TestProcessManager* process_manager_;
  testing::StrictMock<MockMediaRouteProvider> mock_media_route_provider_;
  interfaces::MediaRouterPtr media_router_proxy_;

 private:
  scoped_ptr<TestingProfile> profile_;
  base::MessageLoop message_loop_;
  interfaces::MediaRouteProviderPtr media_route_provider_proxy_;
  scoped_ptr<mojo::Binding<interfaces::MediaRouteProvider>> binding_;
  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(MediaRouterMojoExtensionTest);
};

TEST_F(MediaRouterMojoExtensionTest, DeferredBindingAndSuspension) {
  // DetachRoute is called before *any* extension has connected.
  // It should be queued.
  media_router_->DetachRoute(kRouteId);

  BindMediaRouteProvider();

  base::RunLoop run_loop, run_loop2;
  // |mojo_media_router| signals its readiness to the MR by registering
  // itself via RegisterMediaRouteProvider().
  // Now that the |media_router| and |mojo_media_router| are fully initialized,
  // the queued DetachRoute() call should be executed.
  EXPECT_CALL(provide_handler_, Invoke(testing::Not("")))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
                  run_loop.Quit();
                }));
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_media_route_provider_, DetachRoute(mojo::String(kRouteId)))
      .WillOnce(InvokeWithoutArgs([&run_loop2]() {
                  run_loop2.Quit();
                }));
  RegisterMediaRouteProvider();
  run_loop.Run();
  run_loop2.Run();

  base::RunLoop run_loop3;
  // Extension is suspended and re-awoken.
  ResetMediaRouteProvider();
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(true));
  EXPECT_CALL(*process_manager_, WakeEventPage(kExtensionId, _))
      .WillOnce(testing::DoAll(media::RunCallback<1>(true),
                               InvokeWithoutArgs([&run_loop3]() {
                                 run_loop3.Quit();
                               }),
                               Return(true)));
  media_router_->DetachRoute(kRouteId2);
  run_loop3.Run();

  base::RunLoop run_loop4, run_loop5;
  // RegisterMediaRouteProvider() is called.
  // The queued DetachRoute(kRouteId2) call should be executed.
  EXPECT_CALL(provide_handler_, Invoke(testing::Not("")))
      .WillOnce(InvokeWithoutArgs([&run_loop4]() {
                  run_loop4.Quit();
                }));
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_media_route_provider_, DetachRoute(mojo::String(kRouteId2)))
      .WillOnce(InvokeWithoutArgs([&run_loop5]() {
                  run_loop5.Quit();
                }));
  BindMediaRouteProvider();
  RegisterMediaRouteProvider();
  run_loop4.Run();
  run_loop5.Run();
  ExpectWakeReasonBucketCount(MediaRouteProviderWakeReason::DETACH_ROUTE, 1);
}

TEST_F(MediaRouterMojoExtensionTest, AttemptedWakeupTooManyTimes) {
  BindMediaRouteProvider();

  // DetachRoute is called while extension is suspended. It should be queued.
  // Schedule a component extension wakeup.
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(true));
  EXPECT_CALL(*process_manager_, WakeEventPage(kExtensionId, _))
      .WillOnce(testing::DoAll(media::RunCallback<1>(true), Return(true)));
  media_router_->DetachRoute(kRouteId);
  EXPECT_EQ(1u, media_router_->pending_requests_.size());
  ExpectWakeReasonBucketCount(MediaRouteProviderWakeReason::DETACH_ROUTE, 1);

  // Media route provider fails to connect to media router before extension is
  // suspended again, and |OnConnectionError| is invoked. Retry the wakeup.
  EXPECT_CALL(*process_manager_, WakeEventPage(kExtensionId, _))
      .Times(MediaRouterMojoImpl::kMaxWakeupAttemptCount - 1)
      .WillRepeatedly(
          testing::DoAll(media::RunCallback<1>(true), Return(true)));
  for (int i = 0; i < MediaRouterMojoImpl::kMaxWakeupAttemptCount - 1; ++i)
    media_router_->OnConnectionError();

  // We have already tried |kMaxWakeupAttemptCount| times. If we get an error
  // again, we will give up and the pending request queue will be drained.
  media_router_->OnConnectionError();
  EXPECT_TRUE(media_router_->pending_requests_.empty());
  ExpectWakeReasonBucketCount(MediaRouteProviderWakeReason::CONNECTION_ERROR,
                              MediaRouterMojoImpl::kMaxWakeupAttemptCount - 1);

  // Requests that comes in after queue is drained should be queued.
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(true));
  EXPECT_CALL(*process_manager_, WakeEventPage(kExtensionId, _))
      .WillOnce(testing::DoAll(media::RunCallback<1>(true), Return(true)));
  media_router_->DetachRoute(kRouteId);
  EXPECT_EQ(1u, media_router_->pending_requests_.size());
}

TEST_F(MediaRouterMojoExtensionTest, WakeupFailedDrainsQueue) {
  BindMediaRouteProvider();

  // DetachRoute is called while extension is suspended. It should be queued.
  // Schedule a component extension wakeup.
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(true));
  base::Callback<void(bool)> extension_wakeup_callback;
  EXPECT_CALL(*process_manager_, WakeEventPage(kExtensionId, _))
      .WillOnce(
          testing::DoAll(SaveArg<1>(&extension_wakeup_callback), Return(true)));
  media_router_->DetachRoute(kRouteId);
  EXPECT_EQ(1u, media_router_->pending_requests_.size());

  // Extension wakeup callback returning false is an non-retryable error.
  // Queue should be drained.
  extension_wakeup_callback.Run(false);
  EXPECT_TRUE(media_router_->pending_requests_.empty());

  // Requests that comes in after queue is drained should be queued.
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId))
      .WillOnce(Return(true));
  EXPECT_CALL(*process_manager_, WakeEventPage(kExtensionId, _))
      .WillOnce(testing::DoAll(media::RunCallback<1>(true), Return(true)));
  media_router_->DetachRoute(kRouteId);
  EXPECT_EQ(1u, media_router_->pending_requests_.size());
  ExpectWakeReasonBucketCount(MediaRouteProviderWakeReason::DETACH_ROUTE, 1);
}

TEST_F(MediaRouterMojoExtensionTest, DropOldestPendingRequest) {
  const size_t kMaxPendingRequests = MediaRouterMojoImpl::kMaxPendingRequests;

  // Request is queued.
  media_router_->DetachRoute(kRouteId);
  EXPECT_EQ(1u, media_router_->pending_requests_.size());

  for (size_t i = 0; i < kMaxPendingRequests; ++i)
    media_router_->DetachRoute(kRouteId2);

  // The request queue size should not exceed |kMaxPendingRequests|.
  EXPECT_EQ(kMaxPendingRequests, media_router_->pending_requests_.size());

  base::RunLoop run_loop, run_loop2;
  size_t count = 0;
  // The oldest request should have been dropped, so we don't expect to see
  // DetachRoute(kRouteId) here.
  BindMediaRouteProvider();
  EXPECT_CALL(provide_handler_, Invoke(testing::Not("")))
      .WillOnce(InvokeWithoutArgs([&run_loop]() {
                  run_loop.Quit();
                }));
  EXPECT_CALL(*process_manager_, IsEventPageSuspended(kExtensionId));
  EXPECT_CALL(mock_media_route_provider_, DetachRoute(mojo::String(kRouteId2)))
      .Times(kMaxPendingRequests)
      .WillRepeatedly(InvokeWithoutArgs([&run_loop2, &count]() {
                        if (++count == MediaRouterMojoImpl::kMaxPendingRequests)
                          run_loop2.Quit();
                      }));
  RegisterMediaRouteProvider();
  run_loop.Run();
  run_loop2.Run();
}

}  // namespace media_router
