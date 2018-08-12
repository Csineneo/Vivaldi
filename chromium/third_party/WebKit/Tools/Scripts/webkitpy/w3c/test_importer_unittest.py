# Copyright (C) 2013 Adobe Systems Incorporated. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above
#    copyright notice, this list of conditions and the following
#    disclaimer.
# 2. Redistributions in binary form must reproduce the above
#    copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials
#    provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
# OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

import unittest

from webkitpy.common.host_mock import MockHost
from webkitpy.common.system.executive_mock import MockExecutive, ScriptError
from webkitpy.common.system.filesystem_mock import MockFileSystem
from webkitpy.w3c.test_importer import TestImporter


FAKE_SOURCE_REPO_DIR = '/blink'

FAKE_FILES = {
    '/mock-checkout/third_party/Webkit/LayoutTests/external/OWNERS': '',
    '/blink/w3c/dir/has_shebang.txt': '#!',
    '/blink/w3c/dir/README.txt': '',
    '/blink/w3c/dir/OWNERS': '',
    '/blink/w3c/dir/reftest.list': '',
    '/blink/w3c/dir1/OWNERS': '',
    '/blink/w3c/dir1/reftest.list': '',
    '/mock-checkout/third_party/WebKit/LayoutTests/external/README.txt': '',
    '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations': '',
}


class TestImporterTest(unittest.TestCase):

    def test_import_dir_with_no_tests(self):
        host = MockHost()
        host.executive = MockExecutive(exception=ScriptError('error'))
        host.filesystem = MockFileSystem(files=FAKE_FILES)
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR, 'destination')
        importer.do_import()  # No exception raised.

    def test_path_too_long_true(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files=FAKE_FILES)
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        self.assertTrue(importer.path_too_long(FAKE_SOURCE_REPO_DIR + '/' + ('x' * 150) + '.html'))

    def test_path_too_long_false(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files=FAKE_FILES)
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        self.assertFalse(importer.path_too_long(FAKE_SOURCE_REPO_DIR + '/x.html'))

    def test_does_not_import_owner_files(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files=FAKE_FILES)
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        importer.find_importable_tests()
        self.assertEqual(
            importer.import_list,
            [
                {
                    'copy_list': [
                        {'dest': 'has_shebang.txt', 'src': '/blink/w3c/dir/has_shebang.txt'},
                        {'dest': 'README.txt', 'src': '/blink/w3c/dir/README.txt'}
                    ],
                    'dirname': '/blink/w3c/dir',
                    'jstests': 0,
                    'reftests': 0,
                    'total_tests': 0,
                }
            ])

    def test_does_not_import_reftestlist_file(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files=FAKE_FILES)
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        importer.find_importable_tests()
        self.assertEqual(
            importer.import_list,
            [
                {
                    'copy_list': [
                        {'dest': 'has_shebang.txt', 'src': '/blink/w3c/dir/has_shebang.txt'},
                        {'dest': 'README.txt', 'src': '/blink/w3c/dir/README.txt'}
                    ],
                    'dirname': '/blink/w3c/dir',
                    'jstests': 0,
                    'reftests': 0,
                    'total_tests': 0,
                }
            ])

    def test_files_with_shebang_are_made_executable(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files=FAKE_FILES)
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        importer.do_import()
        self.assertEqual(
            host.filesystem.executable_files,
            set(['/mock-checkout/third_party/WebKit/LayoutTests/external/blink/w3c/dir/has_shebang.txt']))

    def test_ref_test_with_ref_is_copied(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files={
            '/blink/w3c/dir1/my-ref-test.html': '<html><head><link rel="match" href="ref-file.html" />test</head></html>',
            '/blink/w3c/dir1/ref-file.html': '<html><head>test</head></html>',
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations': '',
            '/mock-checkout/third_party/WebKit/Source/core/css/CSSProperties.in': '',
        })
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        importer.find_importable_tests()
        self.assertEqual(
            importer.import_list,
            [
                {
                    'copy_list': [
                        {'src': '/blink/w3c/dir1/ref-file.html', 'dest': 'ref-file.html'},
                        {'src': '/blink/w3c/dir1/ref-file.html', 'dest': 'my-ref-test-expected.html', 'reference_support_info': {}},
                        {'src': '/blink/w3c/dir1/my-ref-test.html', 'dest': 'my-ref-test.html'}
                    ],
                    'dirname': '/blink/w3c/dir1',
                    'jstests': 0,
                    'reftests': 1,
                    'total_tests': 1
                }
            ])

    def test_ref_test_without_ref_is_skipped(self):
        host = MockHost()
        host.filesystem = MockFileSystem(files={
            '/blink/w3c/dir1/my-ref-test.html': '<html><head><link rel="match" href="not-here.html" /></head></html>',
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations': '',
            '/mock-checkout/third_party/WebKit/Source/core/css/CSSProperties.in': '',
        })
        importer = TestImporter(host, FAKE_SOURCE_REPO_DIR)
        importer.find_importable_tests()
        self.assertEqual(importer.import_list, [])

    def test_should_try_to_convert_positive_cases(self):
        self.assertTrue(TestImporter.should_try_to_convert({}, 'foo.css', 'LayoutTests/external/csswg-test/x'))
        self.assertTrue(TestImporter.should_try_to_convert({}, 'foo.htm', 'LayoutTests/external/csswg-test/x'))
        self.assertTrue(TestImporter.should_try_to_convert({}, 'foo.html', 'LayoutTests/external/csswg-test/x'))
        self.assertTrue(TestImporter.should_try_to_convert({}, 'foo.xht', 'LayoutTests/external/csswg-test/x'))
        self.assertTrue(TestImporter.should_try_to_convert({}, 'foo.xhtml', 'LayoutTests/external/csswg-test/x'))

    def test_should_not_try_to_convert_js_test(self):
        self.assertFalse(TestImporter.should_try_to_convert({'is_jstest': True}, 'foo.html', 'LayoutTests/external/csswg-test/x'))

    def test_should_not_try_to_convert_test_in_wpt(self):
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.html', 'LayoutTests/external/wpt/foo'))

    def test_should_not_try_to_convert_other_file_types(self):
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.bar', 'LayoutTests/external/csswg-test/x'))
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.js', 'LayoutTests/external/csswg-test/x'))
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.md', 'LayoutTests/external/csswg-test/x'))
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.png', 'LayoutTests/external/csswg-test/x'))
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.svg', 'LayoutTests/external/csswg-test/x'))
        self.assertFalse(TestImporter.should_try_to_convert({}, 'foo.svgz', 'LayoutTests/external/csswg-test/x'))
