// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('settings_about_page', function() {
  /**
   * @constructor
   * @implements {settings.AboutPageBrowserProxy}
   * @extends {settings.TestBrowserProxy}
   */
  var TestAboutPageBrowserProxy = function() {
    var methodNames = [
      'pageReady',
      'refreshUpdateStatus',
      'openHelpPage',
      'openFeedbackDialog',
    ];

    if (cr.isChromeOS) {
      methodNames.push(
        'getCurrentChannel',
        'getTargetChannel',
        'getVersionInfo',
        'getRegulatoryInfo',
        'setChannel');
    }

    settings.TestBrowserProxy.call(this, methodNames);

    /** @private {!UpdateStatus} */
    this.updateStatus_ = UpdateStatus.UPDATED;

    if (cr.isChromeOS) {
      /** @type {!VersionInfo} */
      this.versionInfo_ = {
        arcVersion: '',
        osFirmware: '',
        osVersion: '',
      };

      /** @private {!BrowserChannel} */
      this.currentChannel_ = BrowserChannel.BETA;

      /** @private {!BrowserChannel} */
      this.targetChannel_ = BrowserChannel.BETA;

      /** @private {?RegulatoryInfo} */
      this.regulatoryInfo_ = null;
    }
  };

  TestAboutPageBrowserProxy.prototype = {
    __proto__: settings.TestBrowserProxy.prototype,

    /** @param {!UpdateStatus} updateStatus */
    setUpdateStatus: function(updateStatus) {
      this.updateStatus_ = updateStatus;
    },

    /** @override */
    pageReady: function() {
      this.methodCalled('pageReady');
    },

    /** @override */
    refreshUpdateStatus: function() {
      cr.webUIListenerCallback('update-status-changed', {
        progress: 1,
        status: this.updateStatus_,
      });
      this.methodCalled('refreshUpdateStatus');
    },

    /** @override */
    openFeedbackDialog: function() {
      this.methodCalled('openFeedbackDialog');
    },

    /** @override */
    openHelpPage: function() {
      this.methodCalled('openHelpPage');
    },
  };

  if (cr.isChromeOS) {
    /** @param {!VersionInfo} */
    TestAboutPageBrowserProxy.prototype.setVersionInfo = function(versionInfo) {
      this.versionInfo_ = versionInfo;
    };

    /**
     * @param {!BrowserChannel} current
     * @param {!BrowserChannel} target
     */
    TestAboutPageBrowserProxy.prototype.setChannels = function(
        current, target) {
      this.currentChannel_ = current;
      this.targetChannel_ = target;
    };


    /** @param {?RegulatoryInfo} regulatoryInfo */
    TestAboutPageBrowserProxy.prototype.setRegulatoryInfo = function(
        regulatoryInfo) {
      this.regulatoryInfo_ = regulatoryInfo;
    };

    /** @override */
    TestAboutPageBrowserProxy.prototype.getCurrentChannel = function() {
      this.methodCalled('getCurrentChannel');
      return Promise.resolve(this.currentChannel_);
    };

    /** @override */
    TestAboutPageBrowserProxy.prototype.getTargetChannel = function() {
      this.methodCalled('getTargetChannel');
      return Promise.resolve(this.targetChannel_);
    };

    /** @override */
    TestAboutPageBrowserProxy.prototype.getVersionInfo = function() {
      this.methodCalled('getVersionInfo');
      return Promise.resolve(this.versionInfo_);
    };

    /** @override */
    TestAboutPageBrowserProxy.prototype.getRegulatoryInfo = function() {
      this.methodCalled('getRegulatoryInfo');
      return Promise.resolve(this.regulatoryInfo_);
    };

    /** @override */
    TestAboutPageBrowserProxy.prototype.setChannel = function(
        channel, isPowerwashAllowed) {
      this.methodCalled('setChannel', [channel, isPowerwashAllowed]);
    };
  }


  function registerAboutPageTests() {
    /**
     * @param {!UpdateStatus} status
     * @param {number=} opt_progress
     */
    function fireStatusChanged(status, opt_progress) {
      cr.webUIListenerCallback('update-status-changed', {
        progress: opt_progress === undefined ? 1 : opt_progress,
        status: status,
      });
    }

    suite('AboutPageTest', function() {
      var page = null;

      /** @type {?settings.TestAboutPageBrowserProxy} */
      var aboutBrowserProxy = null;

      /** @type {?settings.TestLifetimeBrowserProxy} */
      var lifetimeBrowserProxy = null;

      var SPINNER_ICON = 'chrome://resources/images/throbber_small.svg';

      setup(function() {
        lifetimeBrowserProxy = new settings.TestLifetimeBrowserProxy();
        settings.LifetimeBrowserProxyImpl.instance_ = lifetimeBrowserProxy;

        aboutBrowserProxy = new TestAboutPageBrowserProxy();
        settings.AboutPageBrowserProxyImpl.instance_ = aboutBrowserProxy;
        return initNewPage();
      });

      teardown(function() {
        page.remove();
        page = null;
        loadTimeData.overrideValues({
          aboutObsoleteNowOrSoon: false,
          aboutObsoleteEndOfTheLine: false,
        });
      });

      /** @return {!Promise} */
      function initNewPage() {
        aboutBrowserProxy.reset();
        lifetimeBrowserProxy.reset();
        PolymerTest.clearBody();
        page = document.createElement('settings-about-page');
        settings.navigateTo(settings.Route.ABOUT);
        document.body.appendChild(page);
        return aboutBrowserProxy.whenCalled('refreshUpdateStatus');
      }

      /**
       * Test that the status icon and status message update according to
       * incoming 'update-status-changed' events.
       */
      test('IconAndMessageUpdates', function() {
        var icon = page.$$('iron-icon');
        assertTrue(!!icon);
        var statusMessageEl = page.$.updateStatusMessage;
        var previousMessageText = statusMessageEl.textContent;

        fireStatusChanged(UpdateStatus.CHECKING);
        assertEquals(SPINNER_ICON, icon.src);
        assertEquals(null, icon.getAttribute('icon'));
        assertNotEquals(previousMessageText, statusMessageEl.textContent);
        previousMessageText = statusMessageEl.textContent;

        fireStatusChanged(UpdateStatus.UPDATING, 0);
        assertEquals(SPINNER_ICON, icon.src);
        assertEquals(null, icon.getAttribute('icon'));
        assertFalse(statusMessageEl.textContent.includes('%'));
        assertNotEquals(previousMessageText, statusMessageEl.textContent);
        previousMessageText = statusMessageEl.textContent;

        fireStatusChanged(UpdateStatus.UPDATING, 1);
        assertNotEquals(previousMessageText, statusMessageEl.textContent);
        assertTrue(statusMessageEl.textContent.includes('%'));
        previousMessageText = statusMessageEl.textContent;

        fireStatusChanged(UpdateStatus.NEARLY_UPDATED);
        assertEquals(null, icon.src);
        assertEquals('settings:check-circle', icon.icon);
        assertNotEquals(previousMessageText, statusMessageEl.textContent);
        previousMessageText = statusMessageEl.textContent;

        fireStatusChanged(UpdateStatus.DISABLED_BY_ADMIN);
        assertEquals(null, icon.src);
        assertEquals('cr:domain', icon.icon);
        assertEquals(0, statusMessageEl.textContent.trim().length);

        fireStatusChanged(UpdateStatus.FAILED);
        assertEquals(null, icon.src);
        assertEquals('settings:error', icon.icon);
        assertEquals(0, statusMessageEl.textContent.trim().length);

        fireStatusChanged(UpdateStatus.DISABLED);
        assertEquals(null, icon.src);
        assertEquals(null, icon.getAttribute('icon'));
        assertEquals(0, statusMessageEl.textContent.trim().length);
      });

      /**
       * Test that when the current platform has been marked as deprecated (but
       * not end of the line) a deprecation warning message is displayed,
       * without interfering with the update status message and icon.
       */
      test('ObsoleteSystem', function() {
        loadTimeData.overrideValues({
          aboutObsoleteNowOrSoon: true,
          aboutObsoleteEndOfTheLine: false,
        });

        return initNewPage().then(function() {
          var icon = page.$$('iron-icon');
          assertTrue(!!icon);
          assertTrue(!!page.$.updateStatusMessage);
          assertTrue(!!page.$.deprecationWarning);

          assertFalse(page.$.deprecationWarning.hidden);
          assertFalse(page.$.updateStatusMessage.hidden);

          fireStatusChanged(UpdateStatus.CHECKING);
          assertEquals(SPINNER_ICON, icon.src);
          assertEquals(null, icon.getAttribute('icon'));
          assertFalse(page.$.deprecationWarning.hidden);
          assertFalse(page.$.updateStatusMessage.hidden);

          fireStatusChanged(UpdateStatus.UPDATING);
          assertEquals(SPINNER_ICON, icon.src);
          assertEquals(null, icon.getAttribute('icon'));
          assertFalse(page.$.deprecationWarning.hidden);
          assertFalse(page.$.updateStatusMessage.hidden);

          fireStatusChanged(UpdateStatus.NEARLY_UPDATED);
          assertEquals(null, icon.src);
          assertEquals('settings:check-circle', icon.icon);
          assertFalse(page.$.deprecationWarning.hidden);
          assertFalse(page.$.updateStatusMessage.hidden);
        });
      });

      /**
       * Test that when the current platform has reached the end of the line, a
       * deprecation warning message and an error icon is displayed.
       */
      test('ObsoleteSystemEndOfLine', function() {
        loadTimeData.overrideValues({
          aboutObsoleteNowOrSoon: true,
          aboutObsoleteEndOfTheLine: true,
        });
        return initNewPage().then(function() {
          var icon = page.$$('iron-icon');
          assertTrue(!!icon);
          assertTrue(!!page.$.deprecationWarning);
          assertTrue(!!page.$.updateStatusMessage);

          assertFalse(page.$.deprecationWarning.hidden);
          assertFalse(page.$.deprecationWarning.hidden);

          fireStatusChanged(UpdateStatus.CHECKING);
          assertEquals(null, icon.src);
          assertEquals('settings:error', icon.icon);
          assertFalse(page.$.deprecationWarning.hidden);
          assertTrue(page.$.updateStatusMessage.hidden);

          fireStatusChanged(UpdateStatus.FAILED);
          assertEquals(null, icon.src);
          assertEquals('settings:error', icon.icon);
          assertFalse(page.$.deprecationWarning.hidden);
          assertTrue(page.$.updateStatusMessage.hidden);

          fireStatusChanged(UpdateStatus.UPDATED);
          assertEquals(null, icon.src);
          assertEquals('settings:error', icon.icon);
          assertFalse(page.$.deprecationWarning.hidden);
          assertTrue(page.$.updateStatusMessage.hidden);
        });
      });

      test('Relaunch', function() {
        var relaunchContainer = page.$.relaunchContainer;
        assertTrue(!!relaunchContainer);
        assertTrue(relaunchContainer.hidden);

        fireStatusChanged(UpdateStatus.NEARLY_UPDATED);
        assertFalse(relaunchContainer.hidden);

        var relaunch = page.$.relaunch;
        assertTrue(!!relaunch);
        MockInteractions.tap(relaunch);
        return lifetimeBrowserProxy.whenCalled('relaunch');
      });

      if (cr.isChromeOS) {
        /**
         * Test that all buttons update according to incoming
         * 'update-status-changed' events for the case where target and current
         * channel are the same.
         */
        test('ButtonsUpdate_SameChannel', function() {
          var relaunchContainer = page.$.relaunchContainer;
          var checkForUpdates = page.$.checkForUpdates;
          var relaunchAndPowerwash = page.$.relaunchAndPowerwash;

          assertTrue(!!relaunchContainer);
          assertTrue(!!relaunchAndPowerwash);
          assertTrue(!!checkForUpdates);

          function assertAllHidden() {
            assertTrue(checkForUpdates.hidden);
            assertTrue(relaunchContainer.hidden);
            assertTrue(relaunchAndPowerwash.hidden);
          }

          // Check that |UPDATED| status is ignored if the user has not
          // explicitly checked for updates yet.
          fireStatusChanged(UpdateStatus.UPDATED);
          assertFalse(checkForUpdates.hidden);
          assertTrue(relaunchContainer.hidden);
          assertTrue(relaunchAndPowerwash.hidden);

          fireStatusChanged(UpdateStatus.CHECKING);
          assertAllHidden();

          fireStatusChanged(UpdateStatus.UPDATING);
          assertAllHidden();

          fireStatusChanged(UpdateStatus.NEARLY_UPDATED);
          assertTrue(checkForUpdates.hidden);
          assertFalse(relaunchContainer.hidden);
          assertTrue(relaunchAndPowerwash.hidden);

          fireStatusChanged(UpdateStatus.UPDATED);
          assertAllHidden();

          fireStatusChanged(UpdateStatus.FAILED);
          assertFalse(checkForUpdates.hidden);
          assertTrue(relaunchContainer.hidden);
          assertTrue(relaunchAndPowerwash.hidden);

          fireStatusChanged(UpdateStatus.DISABLED);
          assertAllHidden();

          fireStatusChanged(UpdateStatus.DISABLED_BY_ADMIN);
          assertAllHidden();
        });

        /**
         * Test that buttons update according to incoming
         * 'update-status-changed' events for the case where the target channel
         * is more stable than current channel.
         */
        test('ButtonsUpdate_BetaToStable', function() {
          aboutBrowserProxy.setChannels(
              BrowserChannel.BETA, BrowserChannel.STABLE);
          aboutBrowserProxy.setUpdateStatus(UpdateStatus.NEARLY_UPDATED);

          return initNewPage().then(function() {
            assertTrue(!!page.$.relaunchContainer);
            assertTrue(!!page.$.relaunchAndPowerwash);

            assertTrue(page.$.relaunchContainer.hidden);
            assertFalse(page.$.relaunchAndPowerwash.hidden);

            MockInteractions.tap(page.$.relaunchAndPowerwash);
            return lifetimeBrowserProxy.whenCalled('factoryReset');
          });
        });

        /**
         * Test that buttons update according to incoming
         * 'update-status-changed' events for the case where the target channel
         * is less stable than current channel.
         */
        test('ButtonsUpdate_StableToBeta', function() {
          aboutBrowserProxy.setChannels(
              BrowserChannel.STABLE, BrowserChannel.BETA);
          aboutBrowserProxy.setUpdateStatus(UpdateStatus.NEARLY_UPDATED);

          return initNewPage().then(function() {
            assertTrue(!!page.$.relaunchContainer);
            assertTrue(!!page.$.relaunchAndPowerwash);

            assertFalse(page.$.relaunchContainer.hidden);
            assertTrue(page.$.relaunchAndPowerwash.hidden);

            MockInteractions.tap(page.$.relaunch);
            return lifetimeBrowserProxy.whenCalled('relaunch');
          });
        });

        /**
         * Test that buttons update as a result of receiving a
         * 'target-channel-changed' event (normally fired from
         * <settings-channel-switcher-dialog>).
         */
        test('ButtonsUpdate_TargetChannelChangedEvent', function() {
          aboutBrowserProxy.setChannels(
              BrowserChannel.BETA, BrowserChannel.BETA);
          aboutBrowserProxy.setUpdateStatus(UpdateStatus.NEARLY_UPDATED);

          return initNewPage().then(function() {
            assertFalse(page.$.relaunchContainer.hidden);
            assertTrue(page.$.relaunchAndPowerwash.hidden);

            page.fire('target-channel-changed', BrowserChannel.DEV);
            assertFalse(page.$.relaunchContainer.hidden);
            assertTrue(page.$.relaunchAndPowerwash.hidden);

            page.fire('target-channel-changed', BrowserChannel.STABLE);
            assertTrue(page.$.relaunchContainer.hidden);
            assertFalse(page.$.relaunchAndPowerwash.hidden);
          });
        });

        test('RegulatoryInfo', function() {
          var regulatoryInfo = null;

          /**
           * Checks the visibility of the "regulatory info" section.
           * @param {boolean} isShowing Whether the section is expected to be
           *     visible.
           * @return {!Promise}
           */
          function checkRegulatoryInfo(isShowing) {
            return aboutBrowserProxy.whenCalled('getRegulatoryInfo').then(
                function() {
                  var regulatoryInfoEl = page.$.regulatoryInfo;
                  assertTrue(!!regulatoryInfoEl);
                  assertEquals(isShowing, !regulatoryInfoEl.hidden);

                  if (isShowing) {
                    var img = regulatoryInfoEl.querySelector('img');
                    assertTrue(!!img);
                    assertEquals(regulatoryInfo.text, img.getAttribute('alt'));
                    assertEquals(regulatoryInfo.url, img.getAttribute('src'));
                  }
                });
          }

          return checkRegulatoryInfo(false).then(function() {
            regulatoryInfo = {text: 'foo', url: 'bar'};
            aboutBrowserProxy.setRegulatoryInfo(regulatoryInfo);
            return initNewPage();
          }).then(function() {
            return checkRegulatoryInfo(true);
          });
        });
      }

      if (!cr.isChromeOS) {
        /*
         * Test that the "Relaunch" button updates according to incoming
         * 'update-status-changed' events.
         */
        test('ButtonsUpdate', function() {
          var relaunchContainer = page.$.relaunchContainer;
          assertTrue(!!relaunchContainer);

          fireStatusChanged(UpdateStatus.CHECKING);
          assertTrue(relaunchContainer.hidden);

          fireStatusChanged(UpdateStatus.UPDATING);
          assertTrue(relaunchContainer.hidden);

          fireStatusChanged(UpdateStatus.NEARLY_UPDATED);
          assertFalse(relaunchContainer.hidden);

          fireStatusChanged(UpdateStatus.UPDATED);
          assertTrue(relaunchContainer.hidden);

          fireStatusChanged(UpdateStatus.FAILED);
          assertTrue(relaunchContainer.hidden);

          fireStatusChanged(UpdateStatus.DISABLED);
          assertTrue(relaunchContainer.hidden);

          fireStatusChanged(UpdateStatus.DISABLED_BY_ADMIN);
          assertTrue(relaunchContainer.hidden);
        });
      }

      test('GetHelp', function() {
        assertTrue(!!page.$.help);
        MockInteractions.tap(page.$.help);
        return aboutBrowserProxy.whenCalled('openHelpPage');
      });
    });
  }

  function registerOfficialBuildTests() {
    suite('AboutPageTest_OfficialBuild', function() {
      var page = null;
      var browserProxy = null;

      setup(function() {
        browserProxy = new TestAboutPageBrowserProxy();
        settings.AboutPageBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        page = document.createElement('settings-about-page');
        document.body.appendChild(page);
      });

      test('ReportAnIssue', function() {
        assertTrue(!!page.$.reportIssue);
        MockInteractions.tap(page.$.reportIssue);
        return browserProxy.whenCalled('openFeedbackDialog');
      });
    });
  }

  if (cr.isChromeOS) {
    function registerDetailedBuildInfoTests() {
      suite('DetailedBuildInfoTest', function() {
        var page = null;
        var browserProxy = null;

        setup(function() {
          browserProxy = new TestAboutPageBrowserProxy();
          settings.AboutPageBrowserProxyImpl.instance_ = browserProxy;
          PolymerTest.clearBody();
        });

        teardown(function() {
          page.remove();
          page = null;
        });

        test('Initialization', function() {
          var versionInfo = {
            arcVersion: 'dummyArcVersion',
            osFirmware: 'dummyOsFirmware',
            osVersion: 'dummyOsVersion',
          };
          browserProxy.setVersionInfo(versionInfo);

          page = document.createElement('settings-detailed-build-info');
          document.body.appendChild(page);

          return Promise.all([
            browserProxy.whenCalled('pageReady'),
            browserProxy.whenCalled('getVersionInfo'),
            browserProxy.whenCalled('getCurrentChannel'),
          ]).then(function() {
            assertEquals(versionInfo.arcVersion, page.$.arcVersion.textContent);
            assertEquals(versionInfo.osVersion, page.$.osVersion.textContent);
            assertEquals(versionInfo.osFirmware, page.$.osFirmware.textContent);
          });
        });

        /**
         * Checks whether the "change channel" button state (enabled/disabled)
         * correctly reflects whether the user is allowed to change channel (as
         * dictated by the browser via loadTimeData boolean).
         * @param {boolean} canChangeChannel Whether to simulate the case where
         *     changing channels is allowed.
         */
        function checkChangeChannelButton(canChangeChannel) {
          loadTimeData.overrideValues({
            aboutCanChangeChannel: canChangeChannel
          });
          page = document.createElement('settings-detailed-build-info');
          document.body.appendChild(page);

          var changeChannelButton = page.$$('paper-button');
          assertTrue(!!changeChannelButton);
          assertEquals(canChangeChannel, !changeChannelButton.disabled)
        }

        test('ChangeChannel_Enabled', function() {
          checkChangeChannelButton(true);
        });

        test('ChangeChannel_Disabled', function() {
          checkChangeChannelButton(false);
        });
      });
    }

    function registerChannelSwitcherDialogTests() {
      suite('ChannelSwitcherDialogTest', function() {
        var dialog = null;
        var radioButtons = null;
        var browserProxy = null;
        var currentChannel = BrowserChannel.BETA;

        setup(function() {
          browserProxy = new TestAboutPageBrowserProxy();
          browserProxy.setChannels(currentChannel, currentChannel);
          settings.AboutPageBrowserProxyImpl.instance_ = browserProxy;
          PolymerTest.clearBody();
          dialog = document.createElement('settings-channel-switcher-dialog');
          document.body.appendChild(dialog);

          radioButtons = dialog.shadowRoot.querySelectorAll(
              'paper-radio-button');
          assertEquals(3, radioButtons.length);
          return browserProxy.whenCalled('getCurrentChannel');
        });

        teardown(function() { dialog.remove(); });

        test('Initialization', function() {
          var radioGroup = dialog.$$('paper-radio-group');
          assertTrue(!!radioGroup);
          assertTrue(!!dialog.$.warning);
          assertTrue(!!dialog.$.changeChannel);
          assertTrue(!!dialog.$.changeChannelAndPowerwash);

          // Check that upon initialization the radio button corresponding to
          // the current release channel is pre-selected.
          assertEquals(currentChannel, radioGroup.selected);
          assertTrue(dialog.$.warning.hidden);

          // Check that action buttons are hidden when current and target
          // channel are the same.
          assertTrue(dialog.$.changeChannel.hidden);
          assertTrue(dialog.$.changeChannelAndPowerwash.hidden);
        });

        // Test case where user switches to a less stable channel.
        test('ChangeChannel_LessStable', function() {
          assertEquals(BrowserChannel.DEV, radioButtons.item(2).name);
          MockInteractions.tap(radioButtons.item(2));
          Polymer.dom.flush();

          assertFalse(dialog.$.warning.hidden);
          // Check that only the "Change channel" button becomes visible.
          assertTrue(dialog.$.changeChannelAndPowerwash.hidden);
          assertFalse(dialog.$.changeChannel.hidden);

          var whenTargetChannelChangedFired = test_util.eventToPromise(
              'target-channel-changed', dialog);

          MockInteractions.tap(dialog.$.changeChannel);
          return browserProxy.whenCalled('setChannel').then(function(args) {
            assertEquals(BrowserChannel.DEV, args[0]);
            assertFalse(args[1]);
            return whenTargetChannelChangedFired;
          }).then(function(event) {
            assertEquals(BrowserChannel.DEV, event.detail);
          });
        });

        // Test case where user switches to a more stable channel.
        test('ChangeChannel_MoreStable', function() {
          assertEquals(BrowserChannel.STABLE, radioButtons.item(0).name);
          MockInteractions.tap(radioButtons.item(0));
          Polymer.dom.flush();

          assertFalse(dialog.$.warning.hidden);
          // Check that only the "Change channel and Powerwash" button becomes
          // visible.
          assertFalse(dialog.$.changeChannelAndPowerwash.hidden);
          assertTrue(dialog.$.changeChannel.hidden);

          var whenTargetChannelChangedFired = test_util.eventToPromise(
              'target-channel-changed', dialog);

          MockInteractions.tap(dialog.$.changeChannelAndPowerwash);
          return browserProxy.whenCalled('setChannel').then(function(args) {
            assertEquals(BrowserChannel.STABLE, args[0]);
            assertTrue(args[1]);
            return whenTargetChannelChangedFired;
          }).then(function(event) {
            assertEquals(BrowserChannel.STABLE, event.detail);
          });
        });
      });
    }
  }

  return {
    registerTests: function() {
      if (cr.isChromeOS) {
        registerDetailedBuildInfoTests();
        registerChannelSwitcherDialogTests();
      }
      registerAboutPageTests();
    },
    registerOfficialBuildTests: registerOfficialBuildTests,
  };
});
