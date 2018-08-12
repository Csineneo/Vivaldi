# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common utilities used in unit tests, within this directory."""

import devtools_monitor
import loading_model
import loading_trace
import page_track
import request_track
import tracing


class FakeRequestTrack(devtools_monitor.Track):
  def __init__(self, events):
    super(FakeRequestTrack, self).__init__(None)
    self._events = [self._RewriteEvent(e) for e in events]

  def GetEvents(self):
    return self._events

  def _RewriteEvent(self, event):
    # This modifies the instance used across tests, so this method
    # must be idempotent.
    event.timing = event.timing._replace(request_time=event.timestamp)
    return event


class FakePageTrack(devtools_monitor.Track):
  def __init__(self, events):
    super(FakePageTrack, self).__init__(None)
    self._events = events

  def GetEvents(self):
    return self._events

  def GetMainFrameId(self):
    event = self._events[0]
    # Make sure our laziness is not an issue here.
    assert event['method'] == page_track.PageTrack.FRAME_STARTED_LOADING
    return event['frame_id']


def MakeRequest(
    url, source_url, start_time=None, headers_time=None, end_time=None,
    magic_content_type=False, initiator_type='other'):
  """Make a dependent request.

  Args:
    url: a url, or number which will be used as a url.
    source_url: a url or number which will be used as the source (initiating)
      url. If the source url is not present, then url will be a root. The
      convention in tests is to use a source_url of 'null' in this case.
    start_time: The request start time in milliseconds. If None, this is set to
      the current request id in seconds. If None, the two other time parameters
      below must also be None.
    headers_time: The timestamp when resource headers were received, or None.
    end_time: The timestamp when the resource was finished, or None.
    magic_content_type (bool): if true, set a magic content type that makes url
      always be detected as a valid source and destination request.
    initiator_type: the initiator type to use.

  Returns:
    A request_track.Request.

  """
  assert ((start_time is None and
           headers_time is None and
           end_time is None) or
          (start_time is not None and
           headers_time is not None and
           end_time is not None)), \
      'Need no time specified or all times specified'
  if start_time is None:
    # Use the request id in seconds for timestamps. This guarantees increasing
    # times which makes request dependencies behave as expected.
    start_time = headers_time = end_time = MakeRequest._next_request_id * 1000
  assert initiator_type in ('other', 'parser')
  timing = request_track.TimingAsList(request_track.TimingFromDict({
      # connectEnd should be ignored.
      'connectEnd': (end_time - start_time) / 2,
      'receiveHeadersEnd': headers_time - start_time,
      'loadingFinished': end_time - start_time,
      'requestTime': start_time / 1000.0}))
  rq = request_track.Request.FromJsonDict({
      'timestamp': start_time / 1000.0,
      'request_id': str(MakeRequest._next_request_id),
      'url': 'http://' + str(url),
      'initiator': {'type': initiator_type, 'url': 'http://' + str(source_url)},
      'response_headers': {'Content-Type':
                           'null' if not magic_content_type
                           else 'magic-debug-content' },
      'timing': timing
  })
  MakeRequest._next_request_id += 1
  return rq


MakeRequest._next_request_id = 0


def LoadingTraceFromEvents(requests, page_events=None, trace_events=None):
  """Returns a LoadingTrace instance from a list of requests and page events."""
  request = FakeRequestTrack(requests)
  page_event_track = FakePageTrack(page_events if page_events else [])
  if trace_events:
    tracing_track = tracing.TracingTrack(None)
    tracing_track.Handle('Tracing.dataCollected',
                         {'params': {'value': [e for e in trace_events]}})
  else:
    tracing_track = None
  return loading_trace.LoadingTrace(
      None, None, page_event_track, request, tracing_track)


class SimpleLens(object):
  """A simple replacement for RequestDependencyLens.

  Uses only the initiator url of a request for determining a dependency.
  """
  def __init__(self, trace):
    self._trace = trace

  def GetRequestDependencies(self):
    url_to_rq = {}
    deps = []
    for rq in self._trace.request_track.GetEvents():
      assert rq.url not in url_to_rq
      url_to_rq[rq.url] = rq
    for rq in self._trace.request_track.GetEvents():
      initiating_url = rq.initiator['url']
      if initiating_url in url_to_rq:
        deps.append((url_to_rq[initiating_url], rq, rq.initiator['type']))
    return deps


class TestResourceGraph(loading_model.ResourceGraph):
  """Replace the default request lens in a ResourceGraph with our SimpleLens."""
  REQUEST_LENS = SimpleLens

  @classmethod
  def FromRequestList(cls, requests, page_events=None, trace_events=None):
    return cls(LoadingTraceFromEvents(requests, page_events, trace_events))
