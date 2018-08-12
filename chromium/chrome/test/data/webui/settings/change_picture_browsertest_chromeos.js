// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Suite of tests for settings-change-picture. */

GEN_INCLUDE(['settings_page_browsertest.js']);

/**
 * @constructor
 * @extends {SettingsPageBrowserTest}
*/
function SettingsChangePictureBrowserTest() {
}

SettingsChangePictureBrowserTest.prototype = {
  __proto__: SettingsPageBrowserTest.prototype,

  /** @override */
  browsePreload: 'chrome://md-settings/changePicture',

  /** @override */
  preLoad: function() {
    SettingsPageBrowserTest.prototype.preLoad.call(this);

    cr.exportPath('settings_test').changePictureNotifyForTest = true;
  },
};

// Times out on debug builders and may time out on memory bots because
// the Settings page can take several seconds to load in a Release build
// and several times that in a Debug build. See https://crbug.com/558434.
GEN('#if defined(MEMORY_SANITIZER) || !defined(NDEBUG)');
GEN('#define MAYBE_ChangePicture DISABLED_ChangePicture');
GEN('#else');
GEN('#define MAYBE_ChangePicture ChangePicture');
GEN('#endif');

// Runs change picture tests.
TEST_F('SettingsChangePictureBrowserTest', 'MAYBE_ChangePicture', function() {
  var basic = this.getPage('basic');
  assertTrue(!!basic);
  var peopleSection = this.getSection(basic, 'people');
  assertTrue(!!peopleSection);
  var peoplePage = peopleSection.querySelector('settings-people-page');
  assertTrue(!!peoplePage);
  var changePicture = peoplePage.$$('settings-change-picture');
  assertTrue(!!changePicture);
  var settingsCamera = changePicture.$$('settings-camera');
  assertTrue(!!settingsCamera);
  var discardControlBar = changePicture.$.discardControlBar;
  assertTrue(!!discardControlBar);

  /**
   * Returns a promise that resolves once the selected item is updated.
   * @param {function()} action is executed after the listener is set up.
   * @return {!Promise} a Promise fulfilled when the selected item changes.
   */
  function runAndResolveWhenSelectedItemChanged(action) {
    return new Promise(function(resolve, reject) {
      var handler = function() {
        changePicture.removeEventListener('selected-item_-changed', handler);
        resolve();
      };
      changePicture.addEventListener('selected-item_-changed', handler);
      action();
    });
  }

  suite('SettingsChangePicturePage', function() {
    setup(function() {
      // Reset the selected image to nothing.
      changePicture.$.selector.selected = -1;
    });

    test('select camera image', function() {
      var cameraIcon = changePicture.$.cameraImage;
      assertTrue(!!cameraIcon);

      // Force the camera to be absent, even if it's actually present.
      cr.webUIListenerCallback('camera-presence-changed', false);
      Polymer.dom.flush();

      expectTrue(cameraIcon.hidden);
      expectFalse(settingsCamera.cameraActive);

      cr.webUIListenerCallback('camera-presence-changed', true);
      Polymer.dom.flush();

      expectFalse(cameraIcon.hidden);
      expectFalse(settingsCamera.cameraActive);

      MockInteractions.tap(cameraIcon);

      Polymer.dom.flush();
      expectFalse(cameraIcon.hidden);
      expectTrue(settingsCamera.cameraActive);
      expectEquals('camera', changePicture.selectedItem_.dataset.type);
      expectTrue(discardControlBar.hidden);
    });

    test('select profile image', function() {
      var profileImage = changePicture.$.profileImage;
      assertTrue(!!profileImage);

      return runAndResolveWhenSelectedItemChanged(function() {
        MockInteractions.tap(profileImage);
      }).then(function() {
        Polymer.dom.flush();
        expectEquals('profile', changePicture.selectedItem_.dataset.type);
        expectFalse(settingsCamera.cameraActive);
        expectTrue(discardControlBar.hidden);
      });
    });

    test('select old images', function() {
      // By default there is no old image and the element is hidden.
      var oldImage = changePicture.$.oldImage;
      assertTrue(!!oldImage);
      assertTrue(oldImage.hidden);

      return runAndResolveWhenSelectedItemChanged(function() {
        cr.webUIListenerCallback('old-image-changed', 'fake-old-image.jpg');
      }).then(function() {
        Polymer.dom.flush();

        // Expect the old image to be selected once an old image is sent via
        // the native interface.
        expectEquals('old', changePicture.selectedItem_.dataset.type);
        expectFalse(oldImage.hidden);
        expectFalse(settingsCamera.cameraActive);
        expectFalse(discardControlBar.hidden);
      });
    });

    test('select first default image', function() {
      var firstDefaultImage = changePicture.$$('img[data-type="default"]');
      assertTrue(!!firstDefaultImage);

      return runAndResolveWhenSelectedItemChanged(function() {
        MockInteractions.tap(firstDefaultImage);
      }).then(function() {
        Polymer.dom.flush();
        expectEquals('default', changePicture.selectedItem_.dataset.type);
        expectEquals(firstDefaultImage, changePicture.selectedItem_);
        expectFalse(settingsCamera.cameraActive);
        expectTrue(discardControlBar.hidden);
      });
    });

    test('restore image after discard', function() {
      var firstDefaultImage = changePicture.$$('img[data-type="default"]');
      assertTrue(!!firstDefaultImage);
      var discardOldImage = changePicture.$.discardOldImage;
      assertTrue(!!discardOldImage);

      function tapAndVerifyFirstDefaultImage() {
        return runAndResolveWhenSelectedItemChanged(function() {
          MockInteractions.tap(firstDefaultImage);
        }).then(function() {
          Polymer.dom.flush();
          expectEquals(firstDefaultImage, changePicture.selectedItem_);
        });
      }

      function injectAndVerifyOldImage() {
        return runAndResolveWhenSelectedItemChanged(function() {
          cr.webUIListenerCallback('old-image-changed', 'fake-old-image.jpg');
        }).then(function() {
          Polymer.dom.flush();
          expectEquals('old', changePicture.selectedItem_.dataset.type);
        });
      }

      function discardImageAndVerifyFirstDefaultImageSelected() {
        return runAndResolveWhenSelectedItemChanged(function() {
          MockInteractions.tap(discardOldImage);
        }).then(function() {
          Polymer.dom.flush();
          expectEquals(firstDefaultImage, changePicture.selectedItem_);
        });
      }

      return tapAndVerifyFirstDefaultImage()
          .then(injectAndVerifyOldImage)
          .then(discardImageAndVerifyFirstDefaultImageSelected);
    });
  });

  // Run all registered tests.
  mocha.run();
});
