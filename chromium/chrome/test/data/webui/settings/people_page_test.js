// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('settings_people_page', function() {
  /**
   * @constructor
   * @implements {settings.ProfileInfoBrowserProxy}
   * @extends {settings.TestBrowserProxy}
   */
  var TestProfileInfoBrowserProxy = function() {
    settings.TestBrowserProxy.call(this, [
      'getProfileInfo',
      'getProfileManagesSupervisedUsers',
    ]);

    this.fakeProfileInfo = {
      name: 'fakeName',
      iconUrl: 'http://fake-icon-url.com/',
    };
  };

  TestProfileInfoBrowserProxy.prototype = {
    __proto__: settings.TestBrowserProxy.prototype,

    /** @override */
    getProfileInfo: function() {
      this.methodCalled('getProfileInfo');
      return Promise.resolve(this.fakeProfileInfo);
    },

    /** @override */
    getProfileManagesSupervisedUsers: function() {
      this.methodCalled('getProfileManagesSupervisedUsers');
      return Promise.resolve(false);
    }
  };

  /**
   * @constructor
   * @implements {settings.SyncBrowserProxy}
   * @extends {settings.TestBrowserProxy}
   */
  var TestSyncBrowserProxy = function() {
    settings.TestBrowserProxy.call(this, [
      'getSyncStatus',
      'signOut',
    ]);
  };

  TestSyncBrowserProxy.prototype = {
    __proto__: settings.TestBrowserProxy.prototype,

    /** @override */
    getSyncStatus: function() {
      this.methodCalled('getSyncStatus');
      return Promise.resolve({
        signedIn: true,
      });
    },

    /** @override */
    signOut: function(deleteProfile) {
      this.methodCalled('signOut', deleteProfile);
    },
  };

  function registerProfileInfoTests() {
    suite('ProfileInfoTests', function() {
      var peoplePage = null;
      var browserProxy = null;

      suiteSetup(function() {
        // Force easy unlock off. Those have their own ChromeOS-only tests.
        loadTimeData.overrideValues({
          easyUnlockAllowed: false,
        });
      });

      setup(function() {
        browserProxy = new TestProfileInfoBrowserProxy();
        settings.ProfileInfoBrowserProxyImpl.instance_ = browserProxy;

        PolymerTest.clearBody();
        peoplePage = document.createElement('settings-people-page');
        document.body.appendChild(peoplePage);
      });

      teardown(function() { peoplePage.remove(); });

      test('GetProfileInfo', function() {
        return browserProxy.whenCalled('getProfileInfo').then(function() {
          Polymer.dom.flush();
          assertEquals(browserProxy.fakeProfileInfo.name,
                       peoplePage.$$('#profile-name').textContent.trim());
          var bg = peoplePage.$$('#profile-icon').style.backgroundImage;
          assertTrue(bg.includes(browserProxy.fakeProfileInfo.iconUrl));

          cr.webUIListenerCallback(
            'profile-info-changed',
            {name: 'pushedName', iconUrl: 'http://pushed-url/'});

          Polymer.dom.flush();
          assertEquals('pushedName',
                       peoplePage.$$('#profile-name').textContent.trim());
          var newBg = peoplePage.$$('#profile-icon').style.backgroundImage;
          assertTrue(newBg.includes('http://pushed-url/'));
        });
      });

      test('GetProfileManagesSupervisedUsers', function() {
        return browserProxy.whenCalled('getProfileManagesSupervisedUsers').then(
            function() {
              Polymer.dom.flush();
              assertFalse(!!peoplePage.$$('#manageSupervisedUsersContainer'));

              cr.webUIListenerCallback(
                'profile-manages-supervised-users-changed',
                true);

              Polymer.dom.flush();
              assertTrue(!!peoplePage.$$('#manageSupervisedUsersContainer'));
            });
      });
    });
  }

  function registerSyncStatusTests() {
    suite('SyncStatusTests', function() {
      var peoplePage = null;
      var browserProxy = null;

      suiteSetup(function() {
        // Force easy unlock off. Those have their own ChromeOS-only tests.
        loadTimeData.overrideValues({
          easyUnlockAllowed: false,
        });
      });

      setup(function() {
        browserProxy = new TestSyncBrowserProxy();
        settings.SyncBrowserProxyImpl.instance_ = browserProxy;

        PolymerTest.clearBody();
        peoplePage = document.createElement('settings-people-page');
        document.body.appendChild(peoplePage);
      });

      teardown(function() { peoplePage.remove(); });

      test('GetProfileInfo', function() {
        var disconnectButton = null;
        return browserProxy.whenCalled('getSyncStatus').then(function() {
          Polymer.dom.flush();
          disconnectButton = peoplePage.$$('#disconnectButton');
          assertTrue(!!disconnectButton);

          MockInteractions.tap(disconnectButton);
          Polymer.dom.flush();

          assertTrue(peoplePage.$.disconnectDialog.open);
          assertFalse(peoplePage.$.deleteProfile.hidden);

          var disconnectConfirm = peoplePage.$.disconnectConfirm;
          assertTrue(!!disconnectConfirm);
          assertFalse(disconnectConfirm.hidden);

          // Wait for exit of dialog route.
          var dialogExitPromise = new Promise(function(resolve) {
            window.addEventListener('popstate', function callback() {
              window.removeEventListener('popstate', callback);
              resolve();
            });
          });

          MockInteractions.tap(disconnectConfirm);

          return dialogExitPromise;
        }).then(function() {
          return browserProxy.whenCalled('signOut');
        }).then(function(deleteProfile) {
          assertFalse(deleteProfile);

          cr.webUIListenerCallback('sync-status-changed', {
            signedIn: true,
            domain: 'example.com',
          });
          Polymer.dom.flush();

          assertFalse(peoplePage.$.disconnectDialog.open);
          MockInteractions.tap(disconnectButton);
          Polymer.dom.flush();

          assertTrue(peoplePage.$.disconnectDialog.open);
          assertTrue(peoplePage.$.deleteProfile.hidden);

          var disconnectManagedProfileConfirm =
              peoplePage.$.disconnectManagedProfileConfirm;
          assertTrue(!!disconnectManagedProfileConfirm);
          assertFalse(disconnectManagedProfileConfirm.hidden);

          browserProxy.resetResolver('signOut');
          MockInteractions.tap(disconnectManagedProfileConfirm);

          return browserProxy.whenCalled('signOut');
        }).then(function(deleteProfile) {
          assertTrue(deleteProfile);
        });
      });

      test('ActivityControlsLink', function() {
        return browserProxy.whenCalled('getSyncStatus').then(function() {
          Polymer.dom.flush();

          var activityControls = peoplePage.$$('#activity-controls');
          assertTrue(!!activityControls);
          assertFalse(activityControls.hidden);

          cr.webUIListenerCallback('sync-status-changed', {
            signedIn: false,
          });

          assertTrue(activityControls.hidden);
        });
      });

      test('CustomizeSyncDisabledForManagedSignin', function() {
        assertFalse(!!peoplePage.$$('#customize-sync'));

        return browserProxy.whenCalled('getSyncStatus').then(function() {
          cr.webUIListenerCallback('sync-status-changed', {
            signedIn: true,
            syncSystemEnabled: true,
          });
          Polymer.dom.flush();

          var customizeSync = peoplePage.$$('#customize-sync');
          assertTrue(!!customizeSync);
          assertTrue(customizeSync.hasAttribute('actionable'));

          cr.webUIListenerCallback('sync-status-changed', {
            managed: true,
            signedIn: true,
            syncSystemEnabled: true,
          });
          Polymer.dom.flush();

          var customizeSync = peoplePage.$$('#customize-sync');
          assertTrue(!!customizeSync);
          assertFalse(customizeSync.hasAttribute('actionable'));
        });
      });
    });
  }

  return {
    registerTests: function() {
      registerProfileInfoTests();
      if (!cr.isChromeOS)
        registerSyncStatusTests();
    },
  };
});
