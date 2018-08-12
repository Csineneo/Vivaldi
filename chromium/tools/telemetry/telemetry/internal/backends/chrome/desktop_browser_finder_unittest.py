# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import stat
import unittest

from pyfakefs import fake_filesystem_unittest

from telemetry.core import platform
from telemetry.internal.backends.chrome import desktop_browser_finder
from telemetry.internal.browser import browser_options
from telemetry.internal.platform import desktop_device
from telemetry.testing import system_stub


# This file verifies the logic for finding a browser instance on all platforms
# at once. It does so by providing stubs for the OS/sys/subprocess primitives
# that the underlying finding logic usually uses to locate a suitable browser.
# We prefer this approach to having to run the same test on every platform on
# which we want this code to work.

class FindTestBase(unittest.TestCase):
  def setUp(self):
    self._finder_options = browser_options.BrowserFinderOptions()
    self._finder_options.chrome_root = '../../../'
    self._finder_stubs = system_stub.Override(desktop_browser_finder,
                                              ['os', 'subprocess', 'sys'])
    self._path_stubs = system_stub.Override(
        desktop_browser_finder.path_module, ['os', 'sys'])
    self._catapult_path_stubs = system_stub.Override(
        desktop_browser_finder.path_module.catapult_util, ['os', 'sys'])

  def tearDown(self):
    self._finder_stubs.Restore()
    self._path_stubs.Restore()
    self._catapult_path_stubs.Restore()

  @property
  def _files(self):
    return self._catapult_path_stubs.os.path.files

  def DoFindAll(self):
    return desktop_browser_finder.FindAllAvailableBrowsers(
      self._finder_options, desktop_device.DesktopDevice())

  def DoFindAllTypes(self):
    browsers = self.DoFindAll()
    return [b.browser_type for b in browsers]

  def CanFindAvailableBrowsers(self):
    return desktop_browser_finder.CanFindAvailableBrowsers()


def has_type(array, browser_type):
  return len([x for x in array if x.browser_type == browser_type]) != 0


class FindSystemTest(FindTestBase):
  def setUp(self):
    super(FindSystemTest, self).setUp()
    self._finder_stubs.sys.platform = 'win32'
    self._path_stubs.sys.platform = 'win32'

  def testFindProgramFiles(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append(
        'C:\\Program Files\\Vivaldi\\Vivaldi\\Application\\vivaldi.exe')
    self._path_stubs.os.program_files = 'C:\\Program Files'
    self.assertIn('system', self.DoFindAllTypes())

  def testFindProgramFilesX86(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append(
        'C:\\Program Files(x86)\\Vivaldi\\Vivaldi\\Application\\vivaldi.exe')
    self._path_stubs.os.program_files_x86 = 'C:\\Program Files(x86)'
    self.assertIn('system', self.DoFindAllTypes())

  def testFindLocalAppData(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append(
        'C:\\Local App Data\\Vivaldi\\Vivaldi\\Application\\vivaldi.exe')
    self._path_stubs.os.local_app_data = 'C:\\Local App Data'
    self.assertIn('system', self.DoFindAllTypes())


class FindLocalBuildsTest(FindTestBase):
  def setUp(self):
    super(FindLocalBuildsTest, self).setUp()
    self._finder_stubs.sys.platform = 'win32'
    self._path_stubs.sys.platform = 'win32'

  def testFindBuild(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append('..\\..\\..\\..\\build\\Release\\vivaldi.exe')
    self.assertIn('release', self.DoFindAllTypes())

  def testFindOut(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append('..\\..\\..\\..\\out\\Release\\vivaldi.exe')
    self.assertIn('release', self.DoFindAllTypes())

  def testFindXcodebuild(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append('..\\..\\..\\..\\xcodebuild\\Release\\vivaldi.exe')
    self.assertIn('release', self.DoFindAllTypes())


class OSXFindTest(FindTestBase):
  def setUp(self):
    super(OSXFindTest, self).setUp()
    self._finder_stubs.sys.platform = 'darwin'
    self._path_stubs.sys.platform = 'darwin'
    self._files.append('/Applications/Vivaldi Canary.app/'
                       'Contents/MacOS/Vivaldi Canary')
    self._files.append('/Applications/Vivaldi.app/' +
                       'Contents/MacOS/Vivaldi')
    self._files.append(
      '../../../../out/Release/Vivaldi.app/Contents/MacOS/Vivaldi')
    self._files.append(
      '../../../../out/Debug/Vivaldi.app/Contents/MacOS/Vivaldi')
    self._files.append(
      '../../../../out/Release/Content Shell.app/Contents/MacOS/Content Shell')
    self._files.append(
      '../../../../out/Debug/Content Shell.app/Contents/MacOS/Content Shell')

  def testFindAll(self):
    if not self.CanFindAvailableBrowsers():
      return

    types = self.DoFindAllTypes()
    self.assertEquals(
      set(types),
      set(['debug', 'release',
           'content-shell-debug', 'content-shell-release',
           'canary', 'system']))

  def testFindExact(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append(
      '../../../foo1/Vivaldi.app/Contents/MacOS/Vivaldi')
    self._finder_options.browser_executable = (
        '../../../foo1/Vivaldi.app/Contents/MacOS/Vivaldi')
    types = self.DoFindAllTypes()
    self.assertTrue('exact' in types)

  def testCannotFindExact(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append(
      '../../../foo1/Vivaldi.app/Contents/MacOS/Vivaldi')
    self._finder_options.browser_executable = (
        '../../../foo2/Vivaldi.app/Contents/MacOS/Vivaldi')
    self.assertRaises(Exception, self.DoFindAllTypes)

class LinuxFindTest(fake_filesystem_unittest.TestCase):

  def setUp(self):
    if not platform.GetHostPlatform().GetOSName() == 'linux':
      self.skipTest('Not running on Linux')
    self.setUpPyfakefs()

    self._finder_options = browser_options.BrowserFinderOptions()
    self._finder_options.chrome_root = '/src/'

  def CreateBrowser(self, path):
    self.fs.CreateFile(path)
    os.chmod(path, stat.S_IXUSR)

  def DoFindAll(self):
    return desktop_browser_finder.FindAllAvailableBrowsers(
        self._finder_options, desktop_device.DesktopDevice())

  def DoFindAllTypes(self):
    return [b.browser_type for b in self.DoFindAll()]

  def testFindAllWithCheckout(self):
    for target in ['Release', 'Debug']:
      for browser in ['chrome', 'content_shell']:
        self.CreateBrowser('/src/out/%s/%s' % (target, browser))

    self.assertEquals(
        set(self.DoFindAllTypes()),
        {'debug', 'release', 'content-shell-debug', 'content-shell-release'})

  def testFindAllFailsIfNotExecutable(self):
    self.fs.CreateFile('/src/out/Release/vivaldi')

    self.assertFalse(self.DoFindAllTypes())

  def testFindWithProvidedExecutable(self):
    self.CreateBrowser('/foo/vivaldi')
    self._finder_options.browser_executable = '/foo/vivaldi'
    self.assertIn('exact', self.DoFindAllTypes())

  def testFindWithProvidedApk(self):
    self._finder_options.browser_executable = '/foo/chrome.apk'
    self.assertNotIn('exact', self.DoFindAllTypes())

  def testNoErrorWithNonChromeExecutableName(self):
    self.fs.CreateFile('/foo/mandoline')
    self._finder_options.browser_executable = '/foo/mandoline'
    self.assertNotIn('exact', self.DoFindAllTypes())

  def testFindAllWithInstalled(self):
    official_names = ['chrome', 'chrome-beta', 'chrome-unstable']

    for name in official_names:
      self.CreateBrowser('/opt/google/%s/chrome' % name)

    self.assertEquals(set(self.DoFindAllTypes()), {'stable', 'beta', 'dev'})

  def testFindAllSystem(self):
    self.CreateBrowser('/opt/google/chrome/chrome')
    os.symlink('/opt/google/chrome/chrome', '/usr/bin/google-chrome')

    self.assertEquals(set(self.DoFindAllTypes()), {'system', 'stable'})

  def testFindAllSystemIsBeta(self):
    self.CreateBrowser('/opt/google/chrome/chrome')
    self.CreateBrowser('/opt/google/chrome-beta/chrome')
    os.symlink('/opt/google/chrome-beta/chrome', '/usr/bin/google-chrome')

    google_chrome = [browser for browser in self.DoFindAll()
                     if browser.browser_type == 'system'][0]
    self.assertEquals('/opt/vivaldi',
                      google_chrome._browser_directory)


class WinFindTest(FindTestBase):
  def setUp(self):
    super(WinFindTest, self).setUp()

    self._finder_stubs.sys.platform = 'win32'
    self._path_stubs.sys.platform = 'win32'
    self._path_stubs.os.local_app_data = 'c:\\Users\\Someone\\AppData\\Local'
    self._files.append('c:\\tmp\\vivaldi.exe')
    self._files.append('..\\..\\..\\..\\build\\Release\\vivaldi.exe')
    self._files.append('..\\..\\..\\..\\build\\Debug\\vivaldi.exe')
    self._files.append('..\\..\\..\\..\\build\\Release\\content_shell.exe')
    self._files.append('..\\..\\..\\..\\build\\Debug\\content_shell.exe')
    self._files.append(self._path_stubs.os.local_app_data + '\\' +
                       'Vivaldi\\Vivaldi\\Application\\vivaldi.exe')
    self._files.append(self._path_stubs.os.local_app_data + '\\' +
                       'Vivaldi\\Vivaldi SxS\\Application\\vivaldi.exe')

  def testFindAllGivenDefaults(self):
    if not self.CanFindAvailableBrowsers():
      return

    types = self.DoFindAllTypes()
    self.assertEquals(set(types),
                      set(['debug', 'release',
                           'content-shell-debug', 'content-shell-release',
                           'system', 'canary']))

  def testFindAllWithExact(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._finder_options.browser_executable = 'c:\\tmp\\vivaldi.exe'
    types = self.DoFindAllTypes()
    self.assertEquals(
        set(types),
        set(['exact',
             'debug', 'release',
             'content-shell-debug', 'content-shell-release',
             'system', 'canary']))

  def testNoErrorWithUnrecognizedExecutableName(self):
    if not self.CanFindAvailableBrowsers():
      return

    self._files.append('c:\\foo\\mandoline.exe')
    self._finder_options.browser_dir = 'c:\\foo\\mandoline.exe'
    self.assertNotIn('exact', self.DoFindAllTypes())
