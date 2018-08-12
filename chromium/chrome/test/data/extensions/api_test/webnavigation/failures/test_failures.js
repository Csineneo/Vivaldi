// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

onload = function() {
  var getURL = chrome.extension.getURL;
  chrome.tabs.create({"url": "about:blank"}, function(tab) {
    var tabId = tab.id;

    chrome.test.runTests([
      // An page that tries to load an non-existent iframe.
      function nonExistentIframe() {
        expect([
          { label: "a-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 0,
                       parentFrameId: -1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('d.html') }},
          { label: "a-onCommitted",
            event: "onCommitted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       transitionQualifiers: [],
                       transitionType: "link",
                       url: getURL('d.html') }},
          { label: "a-onDOMContentLoaded",
            event: "onDOMContentLoaded",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('d.html') }},
          { label: "a-onCompleted",
            event: "onCompleted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('d.html') }},
          { label: "b-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 1,
                       parentFrameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('c.html') }},
          { label: "b-onErrorOccurred",
            event: "onErrorOccurred",
            details: { error: "net::ERR_FILE_NOT_FOUND",
                       frameId: 1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('c.html') }}],
          [navigationOrder("a-"),
            ["a-onCommitted", "b-onBeforeNavigate", "b-onErrorOccurred",
             "a-onCompleted"]]);
        chrome.tabs.update(tabId, { url: getURL('d.html') });
      },

      // An iframe navigates to a non-existent page.
      function nonExistentIframeNavigation() {
        expect([
          { label: "a-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 0,
                       parentFrameId: -1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('a.html') }},
          { label: "a-onCommitted",
            event: "onCommitted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       transitionQualifiers: [],
                       transitionType: "link",
                       url: getURL('a.html') }},
          { label: "a-onDOMContentLoaded",
            event: "onDOMContentLoaded",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('a.html') }},
          { label: "a-onCompleted",
            event: "onCompleted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('a.html') }},
          { label: "b-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 1,
                       parentFrameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('b.html') }},
          { label: "b-onCommitted",
            event: "onCommitted",
            details: { frameId: 1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       transitionQualifiers: [],
                       transitionType: "auto_subframe",
                       url: getURL('b.html') }},
          { label: "b-onDOMContentLoaded",
            event: "onDOMContentLoaded",
            details: { frameId: 1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('b.html') }},
          { label: "b-onCompleted",
            event: "onCompleted",
            details: { frameId: 1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('b.html') }},
          { label: "c-onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 1,
                       parentFrameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('c.html') }},
          { label: "c-onErrorOccurred",
            event: "onErrorOccurred",
            details: { error: "net::ERR_FILE_NOT_FOUND",
                       frameId: 1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('c.html') }}],
          [ navigationOrder("a-"),
            navigationOrder("b-"),
            isIFrameOf("b-", "a-"),
            isLoadedBy("c-", "b-"),
            ["c-onBeforeNavigate", "c-onErrorOccurred"]]);
        chrome.tabs.update(tabId, { url: getURL('a.html') });
      },

      // Cancel a navigation after it is already committed.
      function cancel() {
        expect([
          { label: "onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 0,
                       parentFrameId: -1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('e.html') }},
          { label: "onCommitted",
            event: "onCommitted",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       transitionQualifiers: [],
                       transitionType: "link",
                       url: getURL('e.html') }},
          { label: "onDOMContentLoaded",
            event: "onDOMContentLoaded",
            details: { frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('e.html') }},
          { label: "onErrorOccurred",
            event: "onErrorOccurred",
            details: { error: "net::ERR_ABORTED",
                       frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('e.html') }}],
          [["onBeforeNavigate", "onCommitted", "onDOMContentLoaded",
            "onErrorOccurred"]]);
        chrome.tabs.update(tabId, { url: getURL('e.html') });
      },

      // Navigates to a non-existent page (this test case must be last,
      // otherwise the non-existant URL breaks tests that follow, since loading
      // those test pages is seen as a non-extension -> extension URL
      // transition, which is forbidden by web_accessible_resources enforcement
      // in manifest version 2.)
      function nonExistent() {
        expect([
          { label: "onBeforeNavigate",
            event: "onBeforeNavigate",
            details: { frameId: 0,
                       parentFrameId: -1,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('nonexistent.html') }},
          { label: "onErrorOccurred",
            event: "onErrorOccurred",
            details: { error: "net::ERR_FILE_NOT_FOUND",
                       frameId: 0,
                       processId: 0,
                       tabId: 0,
                       timeStamp: 0,
                       url: getURL('nonexistent.html') }}],
          [["onBeforeNavigate", "onErrorOccurred"]]);
        chrome.tabs.update(tabId, { url: getURL('nonexistent.html') });
      },
    ]);
  });
};
