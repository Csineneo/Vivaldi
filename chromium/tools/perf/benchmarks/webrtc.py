# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from core import perf_benchmark

from measurements import webrtc
import page_sets


class WebRTC(perf_benchmark.PerfBenchmark):
  """Obtains WebRTC metrics for a real-time video tests."""
  test = webrtc.WebRTC
  page_set = page_sets.WebrtcCasesPageSet
  @classmethod
  def Name(cls):
    return 'webrtc.webrtc_cases'

