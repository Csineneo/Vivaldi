# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import contextlib
import logging
import os
import shutil
import subprocess
import sys
import tempfile
import time

_SRC_DIR = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..', '..'))

sys.path.append(os.path.join(_SRC_DIR, 'third_party', 'catapult', 'devil'))
from devil.android import device_utils
from devil.android import flag_changer
from devil.android import forwarder
from devil.android.sdk import intent

sys.path.append(os.path.join(_SRC_DIR, 'build', 'android'))
from pylib import constants

sys.path.append(os.path.join(_SRC_DIR, 'tools', 'perf'))
from chrome_telemetry_build import chromium_config

sys.path.append(chromium_config.GetTelemetryDir())
from telemetry.internal.util import webpagereplay

sys.path.append(os.path.join(_SRC_DIR, 'third_party', 'webpagereplay'))
import adb_install_cert
import certutils

import chrome_setup
import devtools_monitor
import options


OPTIONS = options.OPTIONS


class DeviceSetupException(Exception):
  def __init__(self, msg):
    super(DeviceSetupException, self).__init__(msg)
    logging.error(msg)


def GetFirstDevice():
  """Returns the first connected device.

  Raises:
    DeviceSetupException if there is no such device.
  """
  devices = device_utils.DeviceUtils.HealthyDevices()
  if not devices:
    raise DeviceSetupException('No devices found')
  return devices[0]


@contextlib.contextmanager
def FlagReplacer(device, command_line_path, new_flags):
  """Replaces chrome flags in a context, restores them afterwards.

  Args:
    device: Device to target, from DeviceUtils. Can be None, in which case this
      context manager is a no-op.
    command_line_path: Full path to the command-line file.
    new_flags: Flags to replace.
  """
  # If we're logging requests from a local desktop chrome instance there is no
  # device.
  if not device:
    yield
    return
  changer = flag_changer.FlagChanger(device, command_line_path)
  changer.ReplaceFlags(new_flags)
  try:
    yield
  finally:
    changer.Restore()


@contextlib.contextmanager
def ForwardPort(device, local, remote):
  """Forwards a local port to a remote one on a device in a context."""
  # If we're logging requests from a local desktop chrome instance there is no
  # device.
  if not device:
    yield
    return
  device.adb.Forward(local, remote)
  try:
    yield
  finally:
    device.adb.ForwardRemove(local)


def _SetUpDevice(device, package_info):
  """Enables root and closes Chrome on a device."""
  device.EnableRoot()
  device.KillAll(package_info.package, quiet=True)


@contextlib.contextmanager
def WprHost(device, wpr_archive_path, record=False,
            network_condition_name=None,
            disable_script_injection=False):
  """Launches web page replay host.

  Args:
    device: Android device.
    wpr_archive_path: host sided WPR archive's path.
    network_condition_name: Network condition name available in
        chrome_setup.NETWORK_CONDITIONS.
    record: Enables or disables WPR archive recording.

  Returns:
    Additional flags list that may be used for chromium to load web page through
    the running web page replay host.
  """
  assert device
  if wpr_archive_path == None:
    assert not record, 'WPR cannot record without a specified archive.'
    assert not network_condition_name, ('WPR cannot emulate network condition' +
                                        ' without a specified archive.')
    yield []
    return

  wpr_server_args = ['--use_closest_match']
  if record:
    wpr_server_args.append('--record')
    if os.path.exists(wpr_archive_path):
      os.remove(wpr_archive_path)
  else:
    assert os.path.exists(wpr_archive_path)
  if network_condition_name:
    condition = chrome_setup.NETWORK_CONDITIONS[network_condition_name]
    if record:
      logging.warning('WPR network condition is ignored when recording.')
    else:
      wpr_server_args.extend([
          '--down', chrome_setup.BandwidthToString(condition['download']),
          '--up', chrome_setup.BandwidthToString(condition['upload']),
          '--delay_ms', str(condition['latency']),
          '--shaping_type', 'proxy'])

  if disable_script_injection:
    # Remove default WPR injected scripts like deterministic.js which
    # overrides Math.random.
    wpr_server_args.extend(['--inject_scripts', ''])

  # Deploy certification authority to the device.
  temp_certificate_dir = tempfile.mkdtemp()
  wpr_ca_cert_path = os.path.join(temp_certificate_dir, 'testca.pem')
  certutils.write_dummy_ca_cert(*certutils.generate_dummy_ca_cert(),
                                cert_path=wpr_ca_cert_path)

  device_cert_util = adb_install_cert.AndroidCertInstaller(
      device.adb.GetDeviceSerial(), None, wpr_ca_cert_path)
  device_cert_util.install_cert(overwrite_cert=True)
  wpr_server_args.extend(['--should_generate_certs',
                          '--https_root_ca_cert_path=' + wpr_ca_cert_path])

  # Set up WPR server and device forwarder.
  wpr_server = webpagereplay.ReplayServer(wpr_archive_path,
      '127.0.0.1', 0, 0, None, wpr_server_args)
  ports = wpr_server.StartServer()[:-1]
  host_http_port = ports[0]
  host_https_port = ports[1]

  forwarder.Forwarder.Map([(0, host_http_port), (0, host_https_port)], device)
  device_http_port = forwarder.Forwarder.DevicePortForHostPort(host_http_port)
  device_https_port = forwarder.Forwarder.DevicePortForHostPort(host_https_port)

  try:
    yield [
      '--host-resolver-rules="MAP * 127.0.0.1,EXCLUDE localhost"',
      '--testing-fixed-http-port={}'.format(device_http_port),
      '--testing-fixed-https-port={}'.format(device_https_port)]
  finally:
    forwarder.Forwarder.UnmapDevicePort(device_http_port, device)
    forwarder.Forwarder.UnmapDevicePort(device_https_port, device)
    wpr_server.StopServer()

    # Remove certification authority from the device.
    device_cert_util.remove_cert()
    shutil.rmtree(temp_certificate_dir)


@contextlib.contextmanager
def _DevToolsConnectionOnDevice(device, flags):
  """Returns a DevToolsConnection context manager for a given device.

  Args:
    device: Device to connect to.
    flags: ([str]) List of flags.

  Returns:
    A DevToolsConnection context manager.
  """
  package_info = OPTIONS.ChromePackage()
  command_line_path = '/data/local/chrome-command-line'
  _SetUpDevice(device, package_info)
  with FlagReplacer(device, command_line_path, flags):
    start_intent = intent.Intent(
        package=package_info.package, activity=package_info.activity,
        data='about:blank')
    device.StartActivity(start_intent, blocking=True)
    time.sleep(2)
    with ForwardPort(device, 'tcp:%d' % OPTIONS.devtools_port,
                     'localabstract:chrome_devtools_remote'):
      yield devtools_monitor.DevToolsConnection(
          OPTIONS.devtools_hostname, OPTIONS.devtools_port)


def DeviceConnection(device, additional_flags=None):
  """Context for starting recording on a device.

  Sets up and restores any device and tracing appropriately

  Args:
    device: Android device, or None for a local run (in which case chrome needs
      to have been started with --remote-debugging-port=XXX).
    additional_flags: Additional chromium arguments.

  Returns:
    A context manager type which evaluates to a DevToolsConnection.
  """
  new_flags = ['--disable-fre',
               '--enable-test-events',
               '--remote-debugging-port=%d' % OPTIONS.devtools_port]
  if OPTIONS.no_sandbox:
    new_flags.append('--no-sandbox')
  if additional_flags != None:
    new_flags.extend(additional_flags)

  if device:
    return _DevToolsConnectionOnDevice(device, new_flags)
  else:
    return chrome_setup.DevToolsConnectionForLocalBinary(new_flags)
