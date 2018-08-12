// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Prototype for Settings page tests. */

/** @const {string} Path to root from chrome/test/data/webui/settings/. */
var ROOT_PATH = '../../../../../';

// Polymer BrowserTest fixture.
GEN_INCLUDE(
    [ROOT_PATH + 'chrome/test/data/webui/polymer_browser_test_base.js']);

/**
 * @constructor
 * @extends {PolymerTest}
*/
function SettingsPageBrowserTest() {}

SettingsPageBrowserTest.prototype = {
  __proto__: PolymerTest.prototype,

  /** @override */
  browsePreload: 'chrome://md-settings/',

  /** @override */
  extraLibraries: PolymerTest.getLibraries(ROOT_PATH).concat([
    '../fake_chrome_event.js',
    'fake_settings_private.js',
  ]),

  /** @override */
  runAccessibilityChecks: false,

  /** @override */
  setUp: function() {
    PolymerTest.prototype.setUp.call(this);
    suiteSetup(function() {
      return CrSettingsPrefs.initialized;
    });
  },

  /**
   * @param {string} type The settings page type, e.g. 'advanced' or 'basic'.
   * @return {!PolymerElement} The PolymerElement for the page.
   */
  getPage: function(type) {
    var settings = document.querySelector('cr-settings');
    assertTrue(!!settings);
    var settingsUi = settings.$$('settings-ui');
    assertTrue(!!settingsUi);
    var settingsMain = settingsUi.$$('settings-main');
    assertTrue(!!settingsMain);
    var pages = settingsMain.$.pageContainer;
    assertTrue(!!pages);
    var pageType = 'settings-' + type + '-page';
    var page = pages.querySelector(pageType);
    assertTrue(!!page);
    return page;
  },

  /**
   * @param {!PolymerElement} page The PolymerElement for the page containing
   *     |section|.
   * @param {string} section The settings page section, e.g. 'appearance'.
   * @return {Node|undefined} The DOM node for the section.
   */
  getSection: function(page, section) {
    var sections = page.shadowRoot.querySelectorAll('settings-section');
    assertTrue(!!sections);
    for (var i = 0; i < sections.length; ++i) {
      var s = sections[i];
      if (s.section == section)
        return s;
    }
    return undefined;
  },
};
