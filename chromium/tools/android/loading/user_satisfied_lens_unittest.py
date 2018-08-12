# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import request_track
import test_utils
import user_satisfied_lens

class UserSatisfiedLensTestCase(unittest.TestCase):
  # We track all times in milliseconds, but raw trace events are in
  # microseconds.
  MILLI_TO_MICRO = 1000

  def setUp(self):
    super(UserSatisfiedLensTestCase, self).setUp()
    self._request_index = 1

  def _RequestAt(self, timestamp_msec, duration=1):
    timestamp_sec = float(timestamp_msec) / 1000
    rq = request_track.Request.FromJsonDict({
        'url': 'http://bla-%s-.com' % timestamp_msec,
        'request_id': '0.%s' % self._request_index,
        'frame_id': '123.%s' % timestamp_msec,
        'initiator': {'type': 'other'},
        'timestamp': timestamp_sec,
        'timing': request_track.TimingFromDict({
            'requestTime': timestamp_sec,
            'loadingFinished': duration})
        })
    self._request_index += 1
    return rq

  def testFirstContentfulPaintLens(self):
    loading_trace = test_utils.LoadingTraceFromEvents(
        [self._RequestAt(1), self._RequestAt(10), self._RequestAt(20)],
        trace_events=[{'ts': 0, 'ph': 'I',
                       'cat': 'blink.some_other_user_timing',
                       'name': 'firstContentfulPaint'},
                      {'ts': 9 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'firstDiscontentPaint'},
                      {'ts': 12 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'firstContentfulPaint'},
                      {'ts': 22 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'firstContentfulPaint'}])
    lens = user_satisfied_lens.FirstContentfulPaintLens(loading_trace)
    self.assertEqual(set(['0.1', '0.2']), lens.CriticalRequests())
    self.assertEqual(1, lens.PostloadTimeMsec())

  def testCantGetNoSatisfaction(self):
    loading_trace = test_utils.LoadingTraceFromEvents(
        [self._RequestAt(1), self._RequestAt(10), self._RequestAt(20)],
        trace_events=[{'ts': 0, 'ph': 'I',
                       'cat': 'not_my_cat',
                       'name': 'someEvent'}])
    lens = user_satisfied_lens.FirstContentfulPaintLens(loading_trace)
    self.assertEqual(set(['0.1', '0.2', '0.3']), lens.CriticalRequests())
    self.assertEqual(float('inf'), lens.PostloadTimeMsec())

  def testFirstTextPaintLens(self):
    loading_trace = test_utils.LoadingTraceFromEvents(
        [self._RequestAt(1), self._RequestAt(10), self._RequestAt(20)],
        trace_events=[{'ts': 0, 'ph': 'I',
                       'cat': 'blink.some_other_user_timing',
                       'name': 'firstPaint'},
                      {'ts': 9 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'firstishPaint'},
                      {'ts': 12 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'firstPaint'},
                      {'ts': 22 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'firstPaint'}])
    lens = user_satisfied_lens.FirstTextPaintLens(loading_trace)
    self.assertEqual(set(['0.1', '0.2']), lens.CriticalRequests())
    self.assertEqual(1, lens.PostloadTimeMsec())

  def testFirstSignificantPaintLens(self):
    loading_trace = test_utils.LoadingTraceFromEvents(
        [self._RequestAt(1), self._RequestAt(10),
         self._RequestAt(15), self._RequestAt(20)],
        trace_events=[{'ts': 0, 'ph': 'I',
                       'cat': 'blink',
                       'name': 'firstPaint'},
                      {'ts': 9 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink.user_timing',
                       'name': 'FrameView::SynchronizedPaint'},
                      {'ts': 18 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink',
                       'name': 'FrameView::SynchronizedPaint'},
                      {'ts': 22 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'blink',
                       'name': 'FrameView::SynchronizedPaint'},

                      {'ts': 5 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'foobar', 'name': 'biz',
                       'args': {'counters': {
                           'LayoutObjectsThatHadNeverHadLayout': 10
                       } } },
                      {'ts': 12 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'foobar', 'name': 'biz',
                       'args': {'counters': {
                           'LayoutObjectsThatHadNeverHadLayout': 12
                       } } },
                      {'ts': 15 * self.MILLI_TO_MICRO, 'ph': 'I',
                       'cat': 'foobar', 'name': 'biz',
                       'args': {'counters': {
                           'LayoutObjectsThatHadNeverHadLayout': 10
                       } } } ])
    lens = user_satisfied_lens.FirstSignificantPaintLens(loading_trace)
    self.assertEqual(set(['0.1', '0.2']), lens.CriticalRequests())
    self.assertEqual(7, lens.PostloadTimeMsec())
