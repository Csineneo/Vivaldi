// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/test_helper.h"

namespace media_router {

MockIssuesObserver::MockIssuesObserver(MediaRouter* router)
    : IssuesObserver(router) {}
MockIssuesObserver::~MockIssuesObserver() {}

MockMediaSinksObserver::MockMediaSinksObserver(MediaRouter* router,
                                               const MediaSource& source)
    : MediaSinksObserver(router, source) {
}
MockMediaSinksObserver::~MockMediaSinksObserver() {
}

MockMediaRoutesObserver::MockMediaRoutesObserver(MediaRouter* router,
    const MediaSource::Id source_id)
    : MediaRoutesObserver(router, source_id) {
}
MockMediaRoutesObserver::~MockMediaRoutesObserver() {
}

MockEventPageTracker::MockEventPageTracker() {
}
MockEventPageTracker::~MockEventPageTracker() {
}

MockPresentationConnectionStateChangedCallback::
    MockPresentationConnectionStateChangedCallback() = default;

MockPresentationConnectionStateChangedCallback::
    ~MockPresentationConnectionStateChangedCallback() = default;

}  // namespace media_router
