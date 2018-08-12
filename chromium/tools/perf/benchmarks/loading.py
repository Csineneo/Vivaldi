# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from core import perf_benchmark
import page_sets

import ct_benchmarks_util
from benchmarks import page_cycler_v2
from telemetry import benchmark
from telemetry.page import cache_temperature
from telemetry.page import traffic_setting


@benchmark.Enabled('android')
class LoadingMobile(perf_benchmark.PerfBenchmark):
  """ A benchmark measuring loading performance of mobile sites. """

  options = {'pageset_repeat': 2}

  def CreateTimelineBasedMeasurementOptions(self):
    return page_cycler_v2.TimelineBasedMeasurementOptionsForLoadingMetric()

  @classmethod
  def ShouldDisable(cls, possible_browser):
    # crbug.com/619254
    if possible_browser.browser_type == 'reference':
      return True

    # crbug.com/651188
    if possible_browser.browser_type == 'android-webview':
      return True

    return False

  @classmethod
  def Name(cls):
    return 'loading.mobile'

  def CreateStorySet(self, options):
    return page_sets.LoadingMobileStorySet(cache_temperatures=[
          cache_temperature.ANY])



# Disabled because we do not plan on running CT benchmarks on the perf
# waterfall any time soon.
@benchmark.Disabled('all')
class LoadingClusterTelemetry(perf_benchmark.PerfBenchmark):

  options = {'upload_results': True}

  _ALL_NET_CONFIGS = traffic_setting.NETWORK_CONFIGS.keys()

  def CreateTimelineBasedMeasurementOptions(self):
    return page_cycler_v2.TimelineBasedMeasurementOptionsForLoadingMetric()

  @classmethod
  def Name(cls):
    return 'loading.cluster_telemetry'

  @classmethod
  def AddBenchmarkCommandLineArgs(cls, parser):
    super(LoadingClusterTelemetry, cls).AddBenchmarkCommandLineArgs(parser)
    ct_benchmarks_util.AddBenchmarkCommandLineArgs(parser)
    parser.add_option(
        '--wait-time',  action='store', type='int',
        default=60, help='Number of seconds to wait for after navigation.')
    parser.add_option(
        '--traffic-setting',  choices=cls._ALL_NET_CONFIGS,
        default=traffic_setting.REGULAR_4G,
        help='Traffic condition (string). Default to "%%default". Can be: %s' %
         ', '.join(cls._ALL_NET_CONFIGS))

  def CreateStorySet(self, options):
    def Wait(action_runner):
      action_runner.Wait(options.wait_time)
    return page_sets.CTPageSet(
      options.urls_list, options.user_agent, options.archive_data_file,
      traffic_setting=options.traffic_setting,
      run_page_interaction_callback=Wait)
