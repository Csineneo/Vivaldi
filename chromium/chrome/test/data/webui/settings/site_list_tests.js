// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Suite of tests for site-list. */
cr.define('site_list', function() {
  function registerTests() {
    suite('SiteList', function() {
      /**
       * A site list element created before each test.
       * @type {SiteList}
       */
      var testElement;

      /**
       * The mock proxy object to use during test.
       * @type {TestSiteSettingsPrefsBrowserProxy}
       */
      var browserProxy = null;

      /**
       * An example pref with 2 blocked location items and 2 allowed. This pref
       * is also used for the All Sites category and therefore needs values for
       * all types, even though some might be blank.
       * @type {SiteSettingsPref}
       */
      var prefs = {
        exceptions: {
          auto_downloads: [],
          background_sync: [],
          camera: [],
          cookies: [],
          fullscreen: [],
          geolocation: [
            {
              embeddingOrigin: 'https://foo-allow.com:443',
              origin: 'https://foo-allow.com:443',
              setting: 'allow',
              source: 'preference',
            },
            {
              embeddingOrigin: 'https://bar-allow.com:443',
              origin: 'https://bar-allow.com:443',
              setting: 'allow',
              source: 'preference',
            },
            {
              embeddingOrigin: 'https://foo-block.com:443',
              origin: 'https://foo-block.com:443',
              setting: 'block',
              source: 'preference',
            },
            {
              embeddingOrigin: 'https://bar-block.com:443',
              origin: 'https://bar-block.com:443',
              setting: 'block',
              source: 'preference',
            },
          ],
          images: [],
          javascript: [],
          keygen: [],
          mic: [],
          notifications: [],
          plugins: [],
          popups: [],
          unsandboxed_plugins: [],
        }
      };

      /**
       * An example pref with mixed schemes (present and absent).
       * @type {SiteSettingsPref}
       */
      var prefsMixedSchemes = {
        exceptions: {
          geolocation: [
            {
              embeddingOrigin: 'https://foo-allow.com',
              origin: 'https://foo-allow.com',
              setting: 'allow',
              source: 'preference',
            },
            {
              embeddingOrigin: 'bar-allow.com',
              origin: 'bar-allow.com',
              setting: 'allow',
              source: 'preference',
            },
          ]
        }
      };

      /**
       * An example pref with mixed origin and pattern.
       * @type {SiteSettingsPref}
       */
      var prefsMixedOriginAndPattern = {
        exceptions: {
          auto_downloads: [],
          background_sync: [],
          camera: [],
          cookies: [],
          fullscreen: [],
          geolocation: [
            {
              origin: 'https://foo.com',
              embeddingOrigin: '*',
              setting: 'allow',
              source: 'preference',
            },
          ],
          images: [],
          javascript: [
            {
              origin: 'https://[*.]foo.com',
              embeddingOrigin: '*',
              setting: 'allow',
              source: 'preference',
            },
          ],
          keygen: [],
          mic: [],
          notifications: [],
          plugins: [],
          popups: [],
          unsandboxed_plugins: [],
        }
      };

      /**
       * An example pref with multiple categories and multiple allow/block
       * state.
       * @type {SiteSettingsPref}
       */
      var prefsVarious = {
        exceptions: {
          auto_downloads: [],
          background_sync: [],
          camera: [],
          cookies: [],
          fullscreen: [],
          geolocation: [
            {
              embeddingOrigin: 'https://foo.com',
              origin: 'https://foo.com',
              setting: 'allow',
              source: 'preference',
            },
            {
              embeddingOrigin: 'https://bar.com',
              origin: 'https://bar.com',
              setting: 'block',
              source: 'preference',
            },
          ],
          images: [],
          javascript: [],
          keygen: [],
          mic: [],
          notifications: [
            {
              embeddingOrigin: 'https://google.com',
              origin: 'https://google.com',
              setting: 'block',
              source: 'preference',
            },
            {
              embeddingOrigin: 'https://bar.com',
              origin: 'https://bar.com',
              setting: 'block',
              source: 'preference',
            },
            {
              embeddingOrigin: 'https://foo.com',
              origin: 'https://foo.com',
              setting: 'block',
              source: 'preference',
            },
          ],
          plugins: [],
          popups: [],
          unsandboxed_plugins: [],
        }
      };

      /**
       * An example pref with 1 allowed location item.
       * @type {SiteSettingsPref}
       */
      var prefsOneEnabled = {
        exceptions: {
          geolocation: [
            {
              embeddingOrigin: 'https://foo-allow.com:443',
              origin: 'https://foo-allow.com:443',
              setting: 'allow',
              source: 'preference',
            },
          ]
        }
      };

      /**
       * An example pref with 1 blocked location item.
       * @type {SiteSettingsPref}
       */
      var prefsOneDisabled = {
        exceptions: {
          geolocation: [
            {
              embeddingOrigin: 'https://foo-block.com:443',
              origin: 'https://foo-block.com:443',
              setting: 'block',
              source: 'preference',
            },
          ]
        }
      };

      /**
       * An example Cookies pref with 1 in each of the three categories.
       * @type {SiteSettingsPref}
       */
      var prefsSessionOnly = {
        exceptions: {
          cookies: [
            {
              embeddingOrigin: 'http://foo-block.com',
              origin: 'http://foo-block.com',
              setting: 'block',
              source: 'preference',
            },
            {
              embeddingOrigin: 'http://foo-allow.com',
              origin: 'http://foo-allow.com',
              setting: 'allow',
              source: 'preference',
            },
            {
              embeddingOrigin: 'http://foo-session.com',
              origin: 'http://foo-session.com',
              setting: 'session_only',
              source: 'preference',
            },
          ]
        }
      };

      /**
       * An example Cookies pref with mixed incognito and regular settings.
       * @type {SiteSettingsPref}
       */
      var prefsIncognito = {
        exceptions: {
          cookies: [
            // foo.com is blocked for regular sessions.
            {
              embeddingOrigin: 'http://foo.com',
              incognito: false,
              origin: 'http://foo.com',
              setting: 'block',
              source: 'preference',
            },
            // foo.com is allowed in incognito (overridden).
            {
              embeddingOrigin: 'http://foo.com',
              incognito: true,
              origin: 'http://foo.com',
              setting: 'allow',
              source: 'preference',
            },
            // bar.com is an allowed incognito item without an embedder.
            {
              embeddingOrigin: '',
              incognito: true,
              origin: 'http://bar.com',
              setting: 'allow',
              source: 'preference',
            },
          ]
        }
      };

      /**
       * An example Javascript pref with a chrome-extension:// scheme.
       * @type {SiteSettingsPref}
       */
      var prefsChromeExtension = {
        exceptions: {
          javascript: [
            {
              embeddingOrigin: '',
              origin: 'chrome-extension://cfhgfbfpcbnnbibfphagcjmgjfjmojfa/',
              setting: 'block',
              source: 'preference',
            },
          ]
        }
      };

      // Import necessary html before running suite.
      suiteSetup(function() {
        CrSettingsPrefs.setInitialized();
        return PolymerTest.importHtml(
            'chrome://md-settings/site_settings/site_list.html');
      });

      suiteTeardown(function() {
        CrSettingsPrefs.resetForTesting();
      });

      // Initialize a site-list before each test.
      setup(function() {
        browserProxy = new TestSiteSettingsPrefsBrowserProxy();
        settings.SiteSettingsPrefsBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        testElement = document.createElement('site-list');
        document.body.appendChild(testElement);
      });

      /**
       * Fetch the non-hidden menu items from the list.
       * @param {!HTMLElement} parentElement
       * @param {number} index The index of the child element (which site) to
       *     fetch.
       */
      function getMenuItems(listContainer, index) {
        return listContainer.children[index].querySelectorAll(
            'iron-dropdown .dropdown-item:not([hidden])');
      }

      /**
       * Opens the action menu for a particular element in the list.
       * @param {number} index The index of the child element (which site) to
       *     open the action menu for.
       */
      function openActionMenu(index) {
        var item = testElement.$.listContainer.children[index];
        var dots = item.querySelector('paper-icon-button');
        MockInteractions.tap(dots);
        Polymer.dom.flush();
      }

      /**
       * Asserts the menu looks as expected.
       * @param {Array<string>} items The items expected to show in the menu.
       * @param {!HTMLElement} parentElement The parent node to start looking
       *     in.
       */
      function assertMenu(items, parentElement) {
        var menuItems = getMenuItems(parentElement.$.listContainer, 0);
        assertEquals(items.length, menuItems.length);
        for (var i = 0; i < items.length; i++)
          assertEquals(items[i], menuItems[i].textContent.trim());
      }

      /**
       * Configures the test element for a particular category.
       * @param {settings.ContentSettingsTypes} category The category to setup.
       * @param {settings.PermissionValues} subtype Type of list to use.
       * @param {Array<dictionary>} prefs The prefs to use.
       */
      function setupCategory(category, subtype, prefs) {
        browserProxy.setPrefs(prefs);
        if (category == settings.ALL_SITES) {
          testElement.categorySubtype = settings.INVALID_CATEGORY_SUBTYPE;
          testElement.allSites = true;
        } else {
          testElement.categorySubtype = subtype;
          testElement.allSites = false;
        }
        // Some route is needed, but the actual route doesn't matter.
        testElement.currentRoute = {
          page: 'dummy',
          section: 'privacy',
          subpage: ['site-settings', 'site-settings-category-location'],
        };
        testElement.category = category;
      }

      test('getExceptionList API used', function() {
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefsEmpty);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);
            });
      });

      test('Empty list', function() {
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefsEmpty);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertEquals(0, testElement.sites.length);

              assertEquals(
                  settings.PermissionValues.ALLOW, testElement.categorySubtype);
              assertEquals('Allow - 0', testElement.$.header.innerText.trim());

              assertFalse(testElement.$.category.hidden);
              browserProxy.resetResolver('getExceptionList');
              testElement.categoryEnabled = false;
              return browserProxy.whenCalled('getExceptionList');
            }).then(function(contentType) {
              assertFalse(testElement.$.category.hidden);
              assertEquals('Exceptions - 0',
                  testElement.$.header.innerText.trim());
            });
      });

      test('initial ALLOW state is correct', function() {
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefs);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertEquals(2, testElement.sites.length);
              assertEquals(prefs.exceptions.geolocation[1].origin,
                  testElement.sites[0].origin);
              assertEquals(
                  settings.PermissionValues.ALLOW, testElement.categorySubtype);
              Polymer.dom.flush();  // Populates action menu.
              openActionMenu(0);
              assertMenu(['Block', 'Remove'], testElement);
              assertEquals('Allow - 2', testElement.$.header.innerText.trim());

              // Site list should show, no matter what category default is set
              // to.
              assertFalse(testElement.$.category.hidden);
              browserProxy.resetResolver('getExceptionList');
              testElement.categoryEnabled = false;
              return browserProxy.whenCalled('getExceptionList');
            }).then(function(contentType) {
              assertFalse(testElement.$.category.hidden);
              assertEquals('Exceptions - 2',
                  testElement.$.header.innerText.trim());
            });
      });

      test('initial BLOCK state is correct', function() {
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.BLOCK, prefs);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertEquals(2, testElement.sites.length);
              assertEquals(prefs.exceptions.geolocation[3].origin,
                  testElement.sites[0].origin);

              assertEquals(
                  settings.PermissionValues.BLOCK, testElement.categorySubtype);
              Polymer.dom.flush();  // Populates action menu.
              openActionMenu(0);
              assertMenu(['Allow', 'Remove'], testElement);
              assertEquals('Block - 2', testElement.$.header.innerText.trim());

              // Site list should only show when category default is enabled.
              assertFalse(testElement.$.category.hidden);
              browserProxy.resetResolver('getExceptionList');
              testElement.categoryEnabled = false;
              return browserProxy.whenCalled('getExceptionList');
            }).then(function(contentType) {
              assertTrue(testElement.$.category.hidden);
            });
      });

      test('initial SESSION ONLY state is correct', function() {
        setupCategory(settings.ContentSettingsTypes.COOKIES,
            settings.PermissionValues.SESSION_ONLY, prefsSessionOnly);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.COOKIES, contentType);

              assertEquals(1, testElement.sites.length);
              assertEquals(prefsSessionOnly.exceptions.cookies[2].origin,
                  testElement.sites[0].origin);

              assertEquals(settings.PermissionValues.SESSION_ONLY,
                  testElement.categorySubtype);
              Polymer.dom.flush();  // Populates action menu.
              openActionMenu(0);
              assertMenu(['Allow', 'Block', 'Remove'], testElement);
              assertEquals('Clear on exit - 1',
                  testElement.$.header.innerText.trim());

              // Site list should show, no matter what category default is set
              // to.
              assertFalse(testElement.$.category.hidden);
              browserProxy.resetResolver('getExceptionList');
              testElement.categoryEnabled = false;
              return browserProxy.whenCalled('getExceptionList');
            }).then(function(contentType) {
              assertFalse(testElement.$.category.hidden);
              assertEquals('Clear on exit - 1', testElement.$.header.innerText);
            });
      });

      test('initial INCOGNITO BLOCK state is correct', function() {
        setupCategory(settings.ContentSettingsTypes.COOKIES,
            settings.PermissionValues.BLOCK, prefsIncognito);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(settings.ContentSettingsTypes.COOKIES, contentType);

              assertEquals(1, testElement.sites.length);
              assertEquals(prefsIncognito.exceptions.cookies[0].origin,
                  testElement.sites[0].origin);

              assertEquals(settings.PermissionValues.BLOCK,
                  testElement.categorySubtype);
              Polymer.dom.flush();  // Populates action menu.
              openActionMenu(0);
              // 'Clear on exit' is visible as this is not an incognito item.
              assertMenu(['Allow', 'Clear on exit', 'Remove'], testElement);
              assertEquals('Block - 1',
                  testElement.$.header.innerText.trim());

              // Select 'Remove from menu'.
              var menuItems = getMenuItems(testElement.$.listContainer, 0);
              assertTrue(!!menuItems);
              MockInteractions.tap(menuItems[2]);
              return browserProxy.whenCalled(
                  'resetCategoryPermissionForOrigin');
            }).then(function(args) {
              assertEquals('http://foo.com', args[0]);
              assertEquals('http://foo.com', args[1]);
              assertEquals(settings.ContentSettingsTypes.COOKIES, args[2]);
              assertFalse(args[3]);  // Incognito.
            });
      });

      test('initial INCOGNITO ALLOW state is correct', function() {
        setupCategory(settings.ContentSettingsTypes.COOKIES,
            settings.PermissionValues.ALLOW, prefsIncognito);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.COOKIES, contentType);

              assertEquals(2, testElement.sites.length);
              assertEquals(prefsIncognito.exceptions.cookies[2].origin,
                  testElement.sites[0].origin);
              assertEquals(prefsIncognito.exceptions.cookies[1].origin,
                  testElement.sites[1].origin);

              assertEquals(settings.PermissionValues.ALLOW,
                  testElement.categorySubtype);
              Polymer.dom.flush();  // Populates action menu.
              openActionMenu(0);
              // 'Clear on exit' is hidden for incognito items.
              assertMenu(['Block', 'Remove'], testElement);
              assertEquals('Allow - 2',
                  testElement.$.header.innerText.trim());

              // Select 'Remove' from menu on 'foo.com'.
              openActionMenu(1);
              var menuItems = getMenuItems(testElement.$.listContainer, 1);
              assertTrue(!!menuItems);
              MockInteractions.tap(menuItems[1]);
              return browserProxy.whenCalled(
                  'resetCategoryPermissionForOrigin');
            }).then(function(args) {
              assertEquals('http://foo.com', args[0]);
              assertEquals('http://foo.com', args[1]);
              assertEquals(settings.ContentSettingsTypes.COOKIES, args[2]);
              assertTrue(args[3]);  // Incognito.
            });
      });

      test('list items shown and clickable when data is present', function() {
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefs);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              // Required for firstItem to be found below.
              Polymer.dom.flush();

              // Validate that the sites gets populated from pre-canned prefs.
              assertEquals(2, testElement.sites.length);
              assertEquals(prefs.exceptions.geolocation[1].origin,
                  testElement.sites[0].origin);
              assertEquals(undefined, testElement.selectedOrigin);

              // Validate that the sites are shown in UI and can be selected.
              var firstItem = testElement.$.listContainer.children[0];
              var clickable = firstItem.querySelector('.middle');
              assertNotEquals(undefined, clickable);
              MockInteractions.tap(clickable);
              assertEquals(prefs.exceptions.geolocation[1].origin,
                  testElement.selectedSite.origin);
            });
      });

      test('Block list open when Allow list is empty', function() {
        // Prefs: One item in Block list, nothing in Allow list.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.BLOCK, prefsOneDisabled);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertFalse(testElement.$.category.hidden);
              assertTrue(testElement.$.category.opened);
            }).then(function() {
              assertNotEquals(0, testElement.$.listContainer.offsetHeight);
            });
      });

      test('Block list closed when Allow list is not empty', function() {
        // Prefs: Items in both Block and Allow list.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.BLOCK, prefs);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertFalse(testElement.$.category.hidden);
              assertFalse(testElement.$.category.opened);
              assertEquals(0, testElement.$.listContainer.offsetHeight);
            });
      });

      test('Allow list is always open (Block list empty)', function() {
        // Prefs: One item in Allow list, nothing in Block list.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefsOneEnabled);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertFalse(testElement.$.category.hidden);
              assertTrue(testElement.$.category.opened);
            }).then(function() {
              assertNotEquals(0, testElement.$.listContainer.offsetHeight);
            });
      });

      test('Allow list is always open (Block list non-empty)', function() {
        // Prefs: Items in both Block and Allow list.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefs);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertFalse(testElement.$.category.hidden);
              assertTrue(testElement.$.category.opened);
            }).then(function() {
              assertNotEquals(0, testElement.$.listContainer.offsetHeight);
            });
      });

      test('Block list not hidden when empty', function() {
        // Prefs: One item in Allow list, nothing in Block list.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.BLOCK, prefsOneEnabled);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertFalse(testElement.$.category.hidden);
            });
      });

      test('Allow list not hidden when empty', function() {
        // Prefs: One item in Block list, nothing in Allow list.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefsOneDisabled);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              assertEquals(
                  settings.ContentSettingsTypes.GEOLOCATION, contentType);

              assertFalse(testElement.$.category.hidden);
            });
      });

      test('All sites category', function() {
        // Prefs: Multiple and overlapping sites.
        setupCategory(settings.ALL_SITES, '', prefsVarious);

        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              // Use resolver to ensure asserts bubble up to the framework with
              // meaningful errors.
              var resolver = new PromiseResolver();
              testElement.async(resolver.resolve);
              return resolver.promise.then(function() {
                // All Sites calls getExceptionList for all categories, starting
                // with Cookies.
                assertEquals(
                    settings.ContentSettingsTypes.COOKIES, contentType);

                // Required for firstItem to be found below.
                Polymer.dom.flush();

                assertTrue(testElement.$.category.opened);
                assertFalse(testElement.$.category.hidden);
                // Validate that the sites gets populated from pre-canned prefs.
                assertEquals(3, testElement.sites.length,
                    'If this fails with 5 instead of the expected 3, then ' +
                    'the de-duping of sites is not working for site_list');
                assertEquals(prefsVarious.exceptions.geolocation[1].origin,
                    testElement.sites[0].origin);
                assertEquals(prefsVarious.exceptions.geolocation[0].origin,
                    testElement.sites[1].origin);
                assertEquals(prefsVarious.exceptions.notifications[0].origin,
                    testElement.sites[2].origin);
                assertEquals(undefined, testElement.selectedOrigin);

                // Validate that the sites are shown in UI and can be selected.
                var firstItem = testElement.$.listContainer.children[1];
                var clickable = firstItem.querySelector('.middle');
                assertNotEquals(undefined, clickable);
                MockInteractions.tap(clickable);
                assertEquals(prefsVarious.exceptions.geolocation[0].origin,
                    testElement.selectedSite.origin);
              });
            });
      });

      test('All sites mixed pattern and origin', function() {
        // Prefs: One site, represented as origin and pattern.
        setupCategory(settings.ALL_SITES, '', prefsMixedOriginAndPattern);

        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              // Use resolver to ensure asserts bubble up to the framework with
              // meaningful errors.
              var resolver = new PromiseResolver();
              testElement.async(resolver.resolve);
              return resolver.promise.then(function() {
                // All Sites calls getExceptionList for all categories, starting
                // with Cookies.
                assertEquals(
                    settings.ContentSettingsTypes.COOKIES, contentType);

                // Required for firstItem to be found below.
                Polymer.dom.flush();

                assertTrue(testElement.$.category.opened);
                assertFalse(testElement.$.category.hidden);
                // Validate that the sites gets populated from pre-canned prefs.
                assertEquals(1, testElement.sites.length,
                    'If this fails with 2 instead of the expected 1, then ' +
                    'the de-duping of sites is not working for site_list');
                assertEquals(
                    prefsMixedOriginAndPattern.exceptions.geolocation[0].origin,
                    testElement.sites[0].originForDisplay);

                assertEquals(undefined, testElement.selectedOrigin);
                // Validate that the sites are shown in UI and can be selected.
                var firstItem = testElement.$.listContainer.children[0];
                var clickable = firstItem.querySelector('.middle');
                assertNotEquals(undefined, clickable);
                MockInteractions.tap(clickable);
                assertEquals(
                    prefsMixedOriginAndPattern.exceptions.geolocation[0].origin,
                    testElement.selectedSite.originForDisplay);
              });
            });
      });

      test('Mixed schemes (present and absent)', function() {
        // Prefs: One item with scheme and one without.
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefsMixedSchemes);
        return browserProxy.whenCalled('getExceptionList').then(
            function(contentType) {
              // No further checks needed. If this fails, it will hang the test.
            });
      });

      test('Select menu item', function() {
        // Test for error: "Cannot read property 'origin' of undefined".
        setupCategory(settings.ContentSettingsTypes.GEOLOCATION,
            settings.PermissionValues.ALLOW, prefs);
        return browserProxy.whenCalled('getExceptionList').then(function(
            contentType) {
          Polymer.dom.flush();
          openActionMenu(0);
          var menuItems = getMenuItems(testElement.$.listContainer, 0);
          assertTrue(!!menuItems);
          MockInteractions.tap(menuItems[0]);
          return browserProxy.whenCalled('setCategoryPermissionForOrigin');
        });
      });

      test('Chrome Extension scheme', function() {
        setupCategory(settings.ContentSettingsTypes.JAVASCRIPT,
            settings.PermissionValues.BLOCK, prefsChromeExtension);
        return browserProxy.whenCalled('getExceptionList').then(function(
            contentType) {
          Polymer.dom.flush();
          openActionMenu(0);
          assertMenu(['Allow', 'Remove'], testElement);

          var menuItems = getMenuItems(testElement.$.listContainer, 0);
          assertTrue(!!menuItems);
          MockInteractions.tap(menuItems[0]);  // Action: Allow.
          return browserProxy.whenCalled('setCategoryPermissionForOrigin');
        }).then(function(args) {
          assertEquals('chrome-extension://cfhgfbfpcbnnbibfphagcjmgjfjmojfa/',
              args[0]);
          assertEquals('', args[1]);
          assertEquals(settings.ContentSettingsTypes.JAVASCRIPT, args[2]);
          assertEquals('allow', args[3]);
        });
      });
    });
  }
  return {
    registerTests: registerTests,
  };
});
