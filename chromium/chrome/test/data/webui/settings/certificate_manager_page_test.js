// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('certificate_manager_page', function() {
  /**
   * A test version of CertificatesBrowserProxy. Provides helper methods
   * for allowing tests to know when a method was called, as well as
   * specifying mock responses.
   *
   * @constructor
   * @implements {settings.CertificatesBrowserProxy}
   * @extends {settings.TestBrowserProxy}
   */
  var TestCertificatesBrowserProxy = function() {
    settings.TestBrowserProxy.call(this, [
      'deleteCertificate',
      'editCaCertificateTrust',
      'exportCertificate',
      'exportPersonalCertificate',
      'exportPersonalCertificatePasswordSelected',
      'getCaCertificateTrust',
      'importCaCertificate',
      'importCaCertificateTrustSelected',
      'importPersonalCertificate',
      'importPersonalCertificatePasswordSelected',
      'importServerCertificate',
      'refreshCertificates',
      'viewCertificate',
    ]);

    /** @private {!CaTrustInfo} */
    this.caTrustInfo_ = {ssl: true, email: true, objSign: true};

    /** @private {?CertificatesError} */
    this.certificatesError_ = null;
  };

  TestCertificatesBrowserProxy.prototype = {
    __proto__: settings.TestBrowserProxy.prototype,

    /**
     * @param {!CaTrustInfo} caTrustInfo
     */
    setCaCertificateTrust: function(caTrustInfo) {
      this.caTrustInfo_ = caTrustInfo;
    },

    /** @override */
    getCaCertificateTrust: function(id) {
      this.methodCalled('getCaCertificateTrust', id);
      return Promise.resolve(this.caTrustInfo_);
    },

    /** @override */
    importServerCertificate: function() {
      this.methodCalled('importServerCertificate');
      return Promise.resolve();
    },

    /** @override */
    importCaCertificate: function() {
      this.methodCalled('importCaCertificate');
      return Promise.resolve('dummyName');
    },

    /** @override */
    importCaCertificateTrustSelected: function(ssl, email, objSign) {
      this.methodCalled('importCaCertificateTrustSelected', {
        ssl: ssl, email: email, objSign: objSign,
      });
      return this.fulfillRequest_();
    },

    /** @override */
    editCaCertificateTrust: function(id, ssl, email, objSign) {
      this.methodCalled('editCaCertificateTrust', {
        id: id, ssl: ssl, email: email, objSign: objSign,
      });
      return this.fulfillRequest_();
    },

    /**
     * Forces some of the browser proxy methods to start returning errors.
     */
    forceCertificatesError: function() {
      this.certificatesError_ = /** @type {!CertificatesError} */ ({
        title: 'DummyError', description: 'DummyDescription'
      });
    },

    /**
     * @return {!Promise} A promise that is resolved or rejected based on the
     * value of |certificatesError_|.
     * @private
     */
    fulfillRequest_: function() {
      return this.certificatesError_ === null ?
          Promise.resolve() : Promise.reject(this.certificatesError_);
    },

    /** @override */
    deleteCertificate: function(id) {
      this.methodCalled('deleteCertificate', id);
      return this.fulfillRequest_();
    },

    /** @override */
    exportPersonalCertificatePasswordSelected: function(password) {
      this.resolverMap_.get(
          'exportPersonalCertificatePasswordSelected').resolve(password);
      return this.fulfillRequest_();
    },

    /** @override */
    importPersonalCertificate: function() {
      this.methodCalled('importPersonalCertificate');
      return Promise.resolve(true);
    },

    /** @override */
    importPersonalCertificatePasswordSelected: function(password) {
      this.resolverMap_.get(
          'importPersonalCertificatePasswordSelected').resolve(password);
      return this.fulfillRequest_();
    },

    /** @override */
    refreshCertificates: function() {
      this.methodCalled('refreshCertificates');
    },

    /** @override */
    viewCertificate: function(id) {
      this.methodCalled('viewCertificate', id);
    },

    /** @override */
    exportCertificate: function(id) {
      this.methodCalled('exportCertificate', id);
    },

    /** @override */
    exportPersonalCertificate: function(id) {
      this.methodCalled('exportPersonalCertificate', id);
      return Promise.resolve();
    },
  };

  /** @return {!Certificate} */
  function createSampleCertificate() {
    return {
      id: 'dummyCertificateId',
      name: 'dummyCertificateName',
      subnodes: [
        createSampleCertificateSubnode()
      ],
    };
  }

  /** @return {!CertificateSubnode} */
  function createSampleCertificateSubnode() {
    return {
      extractable: false,
      id: 'dummySubnodeId',
      name: 'dummySubnodeName',
      policy: false,
      readonly: false,
      untrusted: false,
      urlLocked: false,
    };
  }

  /**
   * Triggers an 'input' event on the given text input field (which triggers
   * validation to occur for password fields being tested in this file).
   * @param {!PaperInputElement} element
   */
  function triggerInputEvent(element) {
    // The actual key code is irrelevant for tests.
    var kSpaceBar = 32;
    MockInteractions.keyEventOn(element, 'input', kSpaceBar);
  }

  /**
   * Converts an event occurrence to a promise.
   * @param {string} eventType
   * @param {!HTMLElement} target
   * @return {!Promise} A promise firing once the event occurs.
   */
  function eventToPromise(eventType, target) {
    return new Promise(function(resolve, reject) {
      target.addEventListener(eventType, resolve);
    });
  }

  function registerCaTrustEditDialogTests() {
    /** @type {?SettingsCaTrustEditDialogElement} */
    var dialog = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    /** @type {!CaTrustInfo} */
    var caTrustInfo = { ssl: true, email: false, objSign: false };

    suite('CaTrustEditDialogTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        browserProxy.setCaCertificateTrust(caTrustInfo);

        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        dialog = document.createElement('settings-ca-trust-edit-dialog');
      });

      teardown(function() { dialog.remove(); });

      test('EditSuccess', function() {
        dialog.model = createSampleCertificateSubnode();
        document.body.appendChild(dialog);

        return browserProxy.whenCalled('getCaCertificateTrust').then(
            function(id) {
              assertEquals(dialog.model.id, id);
              assertEquals(caTrustInfo.ssl, dialog.$.ssl.checked);
              assertEquals(caTrustInfo.email, dialog.$.email.checked);
              assertEquals(caTrustInfo.objSign, dialog.$.objSign.checked);

              // Simulate toggling all checkboxes.
              MockInteractions.tap(dialog.$.ssl);
              MockInteractions.tap(dialog.$.email);
              MockInteractions.tap(dialog.$.objSign);

              // Simulate clicking 'OK'.
              MockInteractions.tap(dialog.$.ok);

              return browserProxy.whenCalled('editCaCertificateTrust');
            }).then(function(args) {
              assertEquals(dialog.model.id, args.id);
              // Checking that the values sent to C++ are reflecting the
              // changes made by the user (toggling all checkboxes).
              assertEquals(caTrustInfo.ssl, !args.ssl);
              assertEquals(caTrustInfo.email, !args.email);
              assertEquals(caTrustInfo.objSign, !args.objSign);
              // Check that the dialog is closed.
              assertFalse(dialog.$.dialog.opened);
            });
      });

      test('ImportSuccess', function() {
        dialog.model = {name: 'Dummy certificate name'};
        document.body.appendChild(dialog);

        assertFalse(dialog.$.ssl.checked);
        assertFalse(dialog.$.email.checked);
        assertFalse(dialog.$.objSign.checked);

        MockInteractions.tap(dialog.$.ssl);
        MockInteractions.tap(dialog.$.email);

        // Simulate clicking 'OK'.
        MockInteractions.tap(dialog.$.ok);
        return browserProxy.whenCalled('importCaCertificateTrustSelected').then(
            function(args) {
              assertTrue(args.ssl);
              assertTrue(args.email);
              assertFalse(args.objSign);
            });
      });

      test('EditError', function() {
        dialog.model = createSampleCertificateSubnode();
        document.body.appendChild(dialog);
        browserProxy.forceCertificatesError();

        var whenErrorEventFired = eventToPromise('certificates-error', dialog);

        return browserProxy.whenCalled('getCaCertificateTrust').then(
            function() {
              MockInteractions.tap(dialog.$.ok);
              return browserProxy.whenCalled('editCaCertificateTrust');
            }).then(
            function() {
              return whenErrorEventFired;
            });
      });
    });
  }

  function registerDeleteDialogTests() {
    /** @type {?SettingsCertificateDeleteConfirmationDialogElement} */
    var dialog = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    /** @type {!CertificateSubnode} */
    var model = createSampleCertificateSubnode();

    suite('CertificateDeleteConfirmationDialogTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        dialog = document.createElement(
            'settings-certificate-delete-confirmation-dialog');
        dialog.model = model;
        dialog.certificateType = settings.CertificateType.PERSONAL;
        document.body.appendChild(dialog);
      });

      teardown(function() { dialog.remove(); });

      test('DeleteSuccess', function() {
        assertTrue(dialog.$.dialog.opened);
        // Check that the dialog title includes the certificate name.
        var titleEl = Polymer.dom(dialog.$.dialog).querySelector('.title');
        assertTrue(titleEl.textContent.includes(model.name));

        // Simulate clicking 'OK'.
        MockInteractions.tap(dialog.$.ok);

        return browserProxy.whenCalled('deleteCertificate').then(
            function(id) {
              assertEquals(model.id, id);
              // Check that the dialog is closed.
              assertFalse(dialog.$.dialog.opened);
            });
      });

      test('DeleteError', function() {
        browserProxy.forceCertificatesError();
        var whenErrorEventFired = eventToPromise('certificates-error', dialog);

        // Simulate clicking 'OK'.
        MockInteractions.tap(dialog.$.ok);
        return browserProxy.whenCalled('deleteCertificate').then(
            function(id) {
              assertEquals(model.id, id);
              // Ensure that the 'error' event was fired.
              return whenErrorEventFired;
            });
      });
    });
  }

  function registerPasswordEncryptDialogTests() {
    /** @type {?SettingsCertificatePasswordEncryptionDialogElement} */
    var dialog = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    /** @type {!CertificateSubnode} */
    var model = createSampleCertificateSubnode();

    var methodName = 'exportPersonalCertificatePasswordSelected';

    suite('CertificatePasswordEncryptionDialogTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        dialog = document.createElement(
            'settings-certificate-password-encryption-dialog');
        dialog.model = model;
        document.body.appendChild(dialog);
      });

      teardown(function() { dialog.remove(); });

      test('EncryptSuccess', function() {
        var passwordInputElements =
            Polymer.dom(dialog.$.dialog).querySelectorAll('paper-input');
        var passwordInputElement = passwordInputElements[0];
        var confirmPasswordInputElement = passwordInputElements[1];

        assertTrue(dialog.$.dialog.opened);
        assertTrue(dialog.$.ok.disabled);

        // Test that the 'OK' button is disabled when the password fields are
        // empty (even though they both have the same value).
        triggerInputEvent(passwordInputElement);
        assertTrue(dialog.$.ok.disabled);

        // Test that the 'OK' button is disabled until the two password fields
        // match.
        passwordInputElement.value = 'foopassword';
        triggerInputEvent(passwordInputElement);
        assertTrue(dialog.$.ok.disabled);
        confirmPasswordInputElement.value = passwordInputElement.value;
        triggerInputEvent(confirmPasswordInputElement);
        assertFalse(dialog.$.ok.disabled);

        // Simulate clicking 'OK'.
        MockInteractions.tap(dialog.$.ok);

        return browserProxy.whenCalled(methodName).then(function(password) {
          assertEquals(passwordInputElement.value, password);
          // Check that the dialog is closed.
          assertFalse(dialog.$.dialog.opened);
        });
      });

      test('EncryptError', function() {
        browserProxy.forceCertificatesError();

        var passwordInputElements =
            Polymer.dom(dialog.$.dialog).querySelectorAll('paper-input');
        var passwordInputElement = passwordInputElements[0];
        passwordInputElement.value = 'foopassword';
        var confirmPasswordInputElement = passwordInputElements[1];
        confirmPasswordInputElement.value = passwordInputElement.value;
        triggerInputEvent(passwordInputElement);

        var whenErrorEventFired = eventToPromise('certificates-error', dialog);
        MockInteractions.tap(dialog.$.ok);

        return browserProxy.whenCalled(methodName).then(function() {
          return whenErrorEventFired;
        });
      });
    });
  }

  function registerPasswordDecryptDialogTests() {
    /** @type {?SettingsCertificatePasswordDecryptionDialogElement} */
    var dialog = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    var methodName = 'importPersonalCertificatePasswordSelected';

    suite('CertificatePasswordDecryptionDialogTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        dialog = document.createElement(
            'settings-certificate-password-decryption-dialog');
        document.body.appendChild(dialog);
      });

      teardown(function() { dialog.remove(); });

      test('DecryptSuccess', function() {
        var passwordInputElement =
            Polymer.dom(dialog.$.dialog).querySelector('paper-input');
        assertTrue(dialog.$.dialog.opened);
        assertTrue(dialog.$.ok.disabled);

        // Test that the 'OK' button is disabled when the password field is
        // empty.
        triggerInputEvent(passwordInputElement);
        assertTrue(dialog.$.ok.disabled);
        passwordInputElement.value = 'foopassword';
        triggerInputEvent(passwordInputElement);
        assertFalse(dialog.$.ok.disabled);

        // Simulate clicking 'OK'.
        MockInteractions.tap(dialog.$.ok);

        return browserProxy.whenCalled(methodName).then(function(password) {
          assertEquals(passwordInputElement.value, password);
          // Check that the dialog is closed.
          assertFalse(dialog.$.dialog.opened);
        });
      });

      test('DecryptError', function() {
        browserProxy.forceCertificatesError();
        // Simulate entering some password.
        var passwordInputElement =
            Polymer.dom(dialog.$.dialog).querySelector('paper-input');
        passwordInputElement.value = 'foopassword';
        triggerInputEvent(passwordInputElement);

        var whenErrorEventFired = eventToPromise('certificates-error', dialog);
        MockInteractions.tap(dialog.$.ok);
        return browserProxy.whenCalled(methodName).then(function() {
          return whenErrorEventFired;
        });
      });
    });
  }

  function registerCertificateSubentryTests() {
    var subentry = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    /**
     * @return {!Promise} A promise firing once a
     *     |settings.CertificateActionEvent| fires.
     */
    var actionEventToPromise = function() {
      return eventToPromise(settings.CertificateActionEvent, subentry);
    };

    suite('CertificateManagerPageTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        subentry = document.createElement('settings-certificate-subentry');
        subentry.model = createSampleCertificateSubnode();
        subentry.certificateType = settings.CertificateType.PERSONAL;
        document.body.appendChild(subentry);

        // Bring up the popup menu for the following tests to use.
        MockInteractions.tap(subentry.$.dots);
        Polymer.dom.flush();
      });

      teardown(function() { subentry.remove(); });

      // Test case where 'View' option is tapped.
      test('MenuOptions_View', function() {
        var viewButton = subentry.shadowRoot.querySelector('#view');
        MockInteractions.tap(viewButton);
        return browserProxy.whenCalled('viewCertificate').then(function(id) {
          assertEquals(subentry.model.id, id);
        });
      });

      // Test that the 'Edit' option is only shown when appropriate and that
      // once tapped the correct event is fired.
      test('MenuOptions_Edit', function() {
        var editButton = subentry.shadowRoot.querySelector('#edit');
        assertTrue(!!editButton);
        // Should be disabled for any certificate type other than
        // CertificateType.CA
        assertTrue(editButton.hidden);
        subentry.certificateType = settings.CertificateType.CA;
        assertFalse(editButton.hidden);

        // Should be disabled if |policy| is true.
        var model = createSampleCertificateSubnode();
        model.policy = true;
        subentry.model = model;
        assertTrue(editButton.hidden);

        subentry.model = createSampleCertificateSubnode();
        var waitForActionEvent = actionEventToPromise();
        MockInteractions.tap(editButton);
        return waitForActionEvent.then(function(event) {
          var detail = /** @type {!CertificateActionEventDetail} */ (
              event.detail);
          assertEquals(settings.CertificateAction.EDIT, detail.action);
          assertEquals(subentry.model.id, detail.subnode.id);
          assertEquals(subentry.certificateType, detail.certificateType);
        });
      });

      // Test that the 'Delete' option is only shown when appropriate and that
      // once tapped the correct event is fired.
      test('MenuOptions_Delete', function() {
        var deleteButton = subentry.shadowRoot.querySelector('#delete');
        assertTrue(!!deleteButton);

        // Should be disabled when 'model.readonly' is true.
        var model = createSampleCertificateSubnode();
        model.readonly = true;
        subentry.model = model;
        assertTrue(deleteButton.hidden);

        // Should be disabled when 'model.policy' is true.
        var model = createSampleCertificateSubnode();
        model.policy = true;
        subentry.model = model;
        assertTrue(deleteButton.hidden);

        subentry.model = createSampleCertificateSubnode();
        var waitForActionEvent = actionEventToPromise();
        MockInteractions.tap(deleteButton);
        return waitForActionEvent.then(function(event) {
          var detail = /** @type {!CertificateActionEventDetail} */ (
              event.detail);
          assertEquals(settings.CertificateAction.DELETE, detail.action);
          assertEquals(subentry.model.id, detail.subnode.id);
        });
      });

      // Test that the 'Export' option is always shown when the certificate type
      // is not PERSONAL and that once tapped the correct event is fired.
      test('MenuOptions_Export', function() {
        subentry.certificateType = settings.CertificateType.SERVER;
        var exportButton = subentry.shadowRoot.querySelector('#export');
        assertTrue(!!exportButton);
        assertFalse(exportButton.hidden);
        MockInteractions.tap(exportButton);
        return browserProxy.whenCalled('exportCertificate').then(function(id) {
          assertEquals(subentry.model.id, id);
        });
      });

      // Test case of exporting a PERSONAL certificate.
      test('MenuOptions_ExportPersonal', function() {
        var exportButton = subentry.shadowRoot.querySelector('#export');
        assertTrue(!!exportButton);

        // Should be disabled when 'model.extractable' is false.
        assertTrue(exportButton.hidden);

        var model = createSampleCertificateSubnode();
        model.extractable = true;
        subentry.model = model;
        assertFalse(exportButton.hidden);

        var waitForActionEvent = actionEventToPromise();
        MockInteractions.tap(exportButton);
        return browserProxy.whenCalled('exportPersonalCertificate').then(
            function(id) {
              assertEquals(subentry.model.id, id);

              // A promise firing once |settings.CertificateActionEvent| is
              // fired.
              return waitForActionEvent;
            }).then(
            function(event) {
              var detail = /** @type {!CertificateActionEventDetail} */ (
                  event.detail);
              assertEquals(
                  settings.CertificateAction.EXPORT_PERSONAL,
                  detail.action);
              assertEquals(subentry.model.id, detail.subnode.id);
            });
      });
    });
  }

  function registerPageTests() {
    /** @type {?SettingsCertificateManagerPageElement} */
    var page = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    /** @enum {number} */
    var CertificateCategoryIndex = {
      PERSONAL: 0,
      SERVER: 1,
      CA: 2,
      OTHER: 3,
    };

    suite('CertificateManagerPageTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        page = document.createElement('settings-certificate-manager-page');
        document.body.appendChild(page);
      });

      teardown(function() { page.remove(); });

      /**
       * Test that the page requests information from the browser on startup and
       * that it gets populated accordingly.
       */
      test('Initialization', function() {
        // Trigger all category tabs to be added to the DOM.
        var paperTabsElement = page.shadowRoot.querySelector('paper-tabs');
        paperTabsElement.selected = CertificateCategoryIndex.PERSONAL;
        Polymer.dom.flush();
        paperTabsElement.selected = CertificateCategoryIndex.SERVER;
        Polymer.dom.flush();
        paperTabsElement.selected = CertificateCategoryIndex.CA;
        Polymer.dom.flush();
        paperTabsElement.selected = CertificateCategoryIndex.OTHER;
        Polymer.dom.flush();
        var certificateLists = page.shadowRoot.querySelectorAll(
            'settings-certificate-list');
        assertEquals(4, certificateLists.length);

        var assertCertificateListLength = function(
            listIndex, expectedSize) {
          var certificateEntries =
              certificateLists[listIndex].shadowRoot.querySelectorAll(
                  'settings-certificate-entry');
          assertEquals(expectedSize, certificateEntries.length);
        };

        assertCertificateListLength(CertificateCategoryIndex.PERSONAL, 0);
        assertCertificateListLength(CertificateCategoryIndex.SERVER, 0);
        assertCertificateListLength(CertificateCategoryIndex.CA, 0);
        assertCertificateListLength(CertificateCategoryIndex.OTHER, 0);

        return browserProxy.whenCalled('refreshCertificates').then(
            function() {
              // Simulate response for personal and CA certificates.
              cr.webUIListenerCallback(
                  'certificates-changed', 'personalCerts',
                  [createSampleCertificate()]);
              cr.webUIListenerCallback(
                  'certificates-changed', 'caCerts',
                  [createSampleCertificate(), createSampleCertificate()]);
              Polymer.dom.flush();

              assertCertificateListLength(CertificateCategoryIndex.PERSONAL, 1);
              assertCertificateListLength(CertificateCategoryIndex.SERVER, 0);
              assertCertificateListLength(CertificateCategoryIndex.CA, 2);
              assertCertificateListLength(CertificateCategoryIndex.OTHER, 0);
            });
      });

      /**
       * Tests that a dialog opens as a response to a
       * settings.CertificateActionEvent.
       * @param {string} dialogTagName The type of dialog to test.
       * @param {CertificateActionEventDetail} eventDetail
       */
      function testDialogOpensOnAction(dialogTagName, eventDetail)  {
        assertFalse(!!page.shadowRoot.querySelector(dialogTagName));
        page.fire(settings.CertificateActionEvent, eventDetail);
        Polymer.dom.flush();
        assertTrue(!!page.shadowRoot.querySelector(dialogTagName));
      }

      test('OpensDialog_DeleteConfirmation', function() {
        testDialogOpensOnAction(
            'settings-certificate-delete-confirmation-dialog',
            /** @type {!CertificateActionEventDetail} */ ({
              action: settings.CertificateAction.DELETE,
              subnode: createSampleCertificateSubnode(),
              certificateType: settings.CertificateType.PERSONAL
            }));
      });

      test('OpensDialog_PasswordEncryption', function() {
        testDialogOpensOnAction(
            'settings-certificate-password-encryption-dialog',
            /** @type {!CertificateActionEventDetail} */ ({
              action: settings.CertificateAction.EXPORT_PERSONAL,
              subnode: createSampleCertificateSubnode(),
              certificateType: settings.CertificateType.PERSONAL
            }));
      });

      test('OpensDialog_PasswordDecryption', function() {
        testDialogOpensOnAction(
            'settings-certificate-password-decryption-dialog',
            /** @type {!CertificateActionEventDetail} */ ({
              action: settings.CertificateAction.IMPORT,
              subnode: createSampleCertificateSubnode(),
              certificateType: settings.CertificateType.PERSONAL
            }));
      });

      test('OpensDialog_CaTrustEdit', function() {
        testDialogOpensOnAction(
            'settings-ca-trust-edit-dialog',
            /** @type {!CertificateActionEventDetail} */ ({
              action: settings.CertificateAction.EDIT,
              subnode: createSampleCertificateSubnode(),
              certificateType: settings.CertificateType.CA
            }));
      });

      test('OpensDialog_CaTrustImport', function() {
        testDialogOpensOnAction(
            'settings-ca-trust-edit-dialog',
            /** @type {!CertificateActionEventDetail} */ ({
              action: settings.CertificateAction.IMPORT,
              subnode: {name: 'Dummy Certificate Name', id: null},
              certificateType: settings.CertificateType.CA
            }));
      });
    });
  }

  function registerCertificateListTests() {
    /** @type {?SettingsCertificateListElement} */
    var element = null;

    /** @type {?TestCertificatesBrowserProxy} */
    var browserProxy = null;

    suite('CertificateListTests', function() {
      setup(function() {
        browserProxy = new TestCertificatesBrowserProxy();
        settings.CertificatesBrowserProxyImpl.instance_ = browserProxy;
        PolymerTest.clearBody();
        element = document.createElement('settings-certificate-list');
        document.body.appendChild(element);
      });

      teardown(function() { element.remove(); });

      /**
       * Tests the "Import" button functionality.
       * @param {!settings.CertificateType} certificateType
       * @param {string} proxyMethodName The name of the proxy method expected
       *     to be called.
       * @param {boolean} actionEventExpected Whether a
       *     settings.CertificateActionEvent is expected to fire as a result
       *     tapping the Import button.
       */
      function testImportForCertificateType(
          certificateType, proxyMethodName, actionEventExpected) {
        element.certificateType = certificateType
        Polymer.dom.flush();

        var importButton = element.$$('paper-button');
        assertTrue(!!importButton);

        var waitForActionEvent = actionEventExpected ?
            eventToPromise(settings.CertificateActionEvent, element) :
            Promise.resolve(null);

        MockInteractions.tap(importButton);
        return browserProxy.whenCalled(proxyMethodName).then(function() {
          return waitForActionEvent;
        }).then(function(event) {
          if (actionEventExpected) {
            assertEquals(
                settings.CertificateAction.IMPORT, event.detail.action);
            assertEquals(certificateType, event.detail.certificateType);
          }
        });
      }

      test('ImportButton_Personal', function() {
        return testImportForCertificateType(
            settings.CertificateType.PERSONAL,
            'importPersonalCertificate', true);
      });

      test('ImportButton_Server', function() {
        return testImportForCertificateType(
            settings.CertificateType.SERVER, 'importServerCertificate',
            false);
      });

      test('ImportButton_CA', function() {
        return testImportForCertificateType(
            settings.CertificateType.CA, 'importCaCertificate', true);
      });
    });
  }

  return {
    registerTests: function() {
      registerCaTrustEditDialogTests();
      registerDeleteDialogTests();
      registerPasswordEncryptDialogTests();
      registerPasswordDecryptDialogTests();
      registerPageTests();
      registerCertificateSubentryTests();
      registerCertificateListTests();
    },
  };
});
