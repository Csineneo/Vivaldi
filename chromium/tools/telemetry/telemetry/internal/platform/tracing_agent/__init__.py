# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


class TracingAgent(object):
  """A tracing agent provided by the platform.

  A tracing agent can gather data with Start() until Stop().
  Before constructing an TracingAgent, check whether it's supported on the
  platform with IsSupported method first.

  NOTE: All subclasses of TracingAgent must not change the constructor's
  parameters so the agents can be dynamically constructed in
  tracing_controller_backend.

  """

  def __init__(self, platform_backend):
    self._platform_backend = platform_backend

  @classmethod
  def IsSupported(cls, platform_backend):
    del platform_backend  # unused
    return False

  def StartAgentTracing(self, config, timeout):
    """ Override to add tracing agent's custom logic to start tracing.

    Depending on trace_options and category_filter, the tracing agent may choose
    to start or not start tracing.

    Args:
      config: tracing_config instance that contains trace_option and
        category_filter
        trace_options: an instance of tracing_options.TracingOptions that
          control which core tracing systems should be enabled.
        category_filter: an instance of
          tracing_category_filter.TracingCategoryFilter
      timeout: number of seconds that this tracing agent should try to start
        tracing until time out.

    Returns:
      True if tracing agent started successfully.
    """
    raise NotImplementedError

  def StopAgentTracing(self, trace_data_builder):
    """ Override to add tracing agent's custom logic to stop tracing.

    StopAgentTracing() should guarantee tracing is stopped, even if there may
    be exception.
    """
    raise NotImplementedError

  def SupportsExplicitClockSync(self):
    """ Override to indicate support of explicit clock syncing. """
    return False

  def RecordClockSyncMarker(self, sync_id):
    """ Override to record clock sync marker.

    Only override if supports explicit clock syncing.
    Args:
      sync_id: Unqiue id for sync event.
    """
    del sync_id # unused
    raise NotImplementedError
