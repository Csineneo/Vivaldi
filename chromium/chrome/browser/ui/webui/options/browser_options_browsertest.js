// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * TestFixture for browser options WebUI testing.
 * @extends {testing.Test}
 * @constructor
 */
function BrowserOptionsWebUITest() {}

BrowserOptionsWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  browsePreload: 'chrome://chrome/settings/',
};

// Test opening the browser options has correct location.
// Times out on Mac debug only. See http://crbug.com/121030
// TODO(vivaldi) Reenable for Vivaldi
GEN('#if 1 || (defined(OS_MACOSX) && !defined(NDEBUG))');
GEN('#define MAYBE_testOpenBrowserOptions ' +
    'DISABLED_testOpenBrowserOptions');
GEN('#else');
GEN('#define MAYBE_testOpenBrowserOptions testOpenBrowserOptions');
GEN('#endif  // defined(OS_MACOSX)');
TEST_F('BrowserOptionsWebUITest', 'MAYBE_testOpenBrowserOptions', function() {
  assertEquals(this.browsePreload, document.location.href);
  expectFalse($('navigation').classList.contains('background'));
});

/**
 * TestFixture for the uber page when the browser options page has an overlay.
 * @extends {testing.Test}
 * @constructor
 */
function BrowserOptionsOverlayWebUITest() {}

BrowserOptionsOverlayWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  browsePreload: 'chrome://chrome/settings/autofill',

  /** @override */
  isAsync: true,
};

// In Vivaldi this will always break since the relevant code has been removed
TEST_F('BrowserOptionsOverlayWebUITest', 'DISABLED_testNavigationInBackground',
    function() {
  assertEquals(this.browsePreload, document.location.href);

  if ($('navigation').classList.contains('background')) {
    testDone();
    return;
  }

  // Wait for the message to be posted to the Uber page.
  window.addEventListener('message', function(e) {
    if (e.data.method == 'beginInterceptingEvents') {
      window.setTimeout(function() {
        assertTrue($('navigation').classList.contains('background'));
        testDone();
      });
    }
  });
});

/**
 * @extends {testing.Test}
 * @constructor
 */
function BrowserOptionsFrameWebUITest() {}

BrowserOptionsFrameWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  browsePreload: 'chrome://settings-frame/',
};

TEST_F('BrowserOptionsFrameWebUITest', 'testAdvancedSettingsHiddenByDefault',
    function() {
  assertEquals(this.browsePreload, document.location.href);
  expectTrue($('advanced-settings').hidden);
});

/**
 * @extends {testing.Test}
 * @constructor
 */
function AdvancedSettingsWebUITest() {}

AdvancedSettingsWebUITest.prototype = {
  __proto__: testing.Test.prototype,

  /** @override */
  browsePreload: 'chrome://settings-frame/autofill',
};

TEST_F('AdvancedSettingsWebUITest', 'testAdvancedSettingsShown', function() {
  assertEquals(this.browsePreload, document.location.href);
  expectFalse($('advanced-settings').hidden);
});
