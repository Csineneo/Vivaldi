// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


/**
 * This URL is defined in content/public/test/test_mojo_app.cc. It identifies
 * content_shell's out-of-process Mojo test app.
 */
var TEST_APP_URL = 'system:content_mojo_test';


define('main', [
  'mojo/public/js/core',
  'mojo/public/js/router',
  'mojo/shell/public/interfaces/connector.mojom',
  'content/public/renderer/frame_service_registry',
  'content/public/test/test_mojo_service.mojom',
], function (core, router, connectorMojom, serviceRegistry, testMojom) {

  var connectToService = function(serviceProvider, iface) {
    var pipe = core.createMessagePipe();
    var service = new iface.proxyClass(new router.Router(pipe.handle0));
    serviceProvider.getInterface(iface.name, pipe.handle1);
    return service;
  };

  return function() {
    domAutomationController.setAutomationId(0);
    var connectorPipe =
        serviceRegistry.connectToService(connectorMojom.Connector.name);
    var connector = new connectorMojom.Connector.proxyClass(
        new router.Router(connectorPipe));

    var identity = {};
    identity.name = TEST_APP_URL;
    identity.user_id = connectorMojom.kInheritUserID;
    identity.instance = "";
    connector.connect(
        identity,
        function (services) {
          var test = connectToService(services, testMojom.TestMojoService);
          test.getRequestorName().then(function(response) {
            domAutomationController.send(
                response.name == 'chrome://mojo-web-ui/');
          });
        },
        function (exposedServices) {},
        null);
  };
});
