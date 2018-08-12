# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'conditions': [
    # The iOS frameworks being built for ios_web_shell_test require certs which
    # bots do not currently have installed.  Ninja allows this, Xcode does not.
    ['"<(GENERATOR)"=="ninja" or "<(GENERATOR_FLAVOR)"=="ninja"', {
      'targets': [
        {
          'variables': {
            'test_host': 'ios_web_shell.app/ios_web_shell',
          },
          'target_name': 'ios_web_shell_test',
          'type': 'loadable_module',
          'mac_xctest_bundle': 1,
          'dependencies': [
            '../third_party/earl_grey/earl_grey.gyp:EarlGrey',
            'ios_web_shell.gyp:ios_web_shell',
          ],
          'sources': [
            'shell/test/web_shell_navigation_egtest.mm',
          ],
          'xcode_settings': {
            'WRAPPER_EXTENSION': 'xctest',
            'TEST_HOST': '<(test_host)',
            'BUNDLE_LOADER': '$(TEST_HOST)',
            'conditions':[
              ['"<(GENERATOR)"!="xcode" or "<(GENERATOR_FLAVOR)"=="ninja"', {
                'OTHER_LDFLAGS': [
                  '-bundle_loader <@(test_host)',
                ],
              }],
            ],
          },
          'link_settings': {
            'libraries': [
              'CoreGraphics.framework',
              'Foundation.framework',
              'QuartzCore.framework',
              'UIKit.framework',
              'XCTest.framework',
            ],
          },
        },
      ],
    }, { # GENERATOR == ninja or GENERATOR_FLAVOR == ninja
      'targets': [
        {
          # The iOS frameworks being built for ios_web_shell_test require certs
          # which bot do not currently have installed.  Ninja allows this, Xcode
          # does not.
          'target_name': 'ios_web_shell_test',
          'type': 'none',
        },
      ],
    }],
  ],
}
