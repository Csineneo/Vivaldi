# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Models for loading in chrome.

(Redirect the following to the general model module once we have one)
A model is an object with the following methods.
  CostMs(): return the cost of the model in milliseconds.
  Set(): set model-specific parameters.

ResourceGraph
  This creates a DAG of resource dependencies from loading.log_requests to model
  loading time. The model may be parameterized by changing the loading time of
  a particular or all resources.
"""

import logging
import os
import urlparse
import sys

import activity_lens
import dag
import loading_trace
import request_dependencies_lens
import request_track

class ResourceGraph(object):
  """A model of loading by a DAG of resource dependencies.

  See model parameters in Set().
  """
  # The lens to build request dependencies. Exposed here for subclasses in
  # unittesting.
  REQUEST_LENS = request_dependencies_lens.RequestDependencyLens

  EDGE_KIND_KEY = 'edge_kind'
  EDGE_KINDS = request_track.Request.INITIATORS + (
      'script_inferred', 'after-load', 'before-load', 'timing')

  def __init__(self, trace, content_lens=None, frame_lens=None,
               activity=None):
    """Create from a LoadingTrace (or json of a trace).

    Args:
      trace: (LoadingTrace/JSON) Loading trace or JSON of a trace.
      content_lens: (ContentClassificationLens) Lens used to annotate the
                    nodes, or None.
      frame_lens: (FrameLoadLens) Lens used to augment graph with load nodes.
      activity:   (ActivityLens) Lens used to augment the edges with the
                   activity.
    """
    if type(trace) == dict:
      trace = loading_trace.LoadingTrace.FromJsonDict(trace)
    self._trace = trace
    self._content_lens = content_lens
    self._frame_lens = frame_lens
    self._activity_lens = activity
    self._BuildDag(trace)
    # Sort before splitting children so that we can correctly dectect if a
    # reparented child is actually a dependency for a child of its new parent.
    try:
      for n in dag.TopologicalSort(self._nodes):
        self._SplitChildrenByTime(self._node_info[n.Index()])
    except AssertionError as exc:
      sys.stderr.write('Bad topological sort: %s\n'
                       'Skipping child split\n' % str(exc))
    self._cache_all = False
    self._node_filter = lambda _: True

  @classmethod
  def CheckImageLoadConsistency(cls, g1, g2):
    """Check that images have the same dependencies between ResourceGraphs.

    Image resources are identified by their short names.

    Args:
      g1: a ResourceGraph instance
      g2: a ResourceGraph instance

    Returns:
      A list of discrepancy tuples. If this list is empty, g1 and g2 are
      consistent with respect to image load dependencies. Otherwise, each tuple
      is of the form:
        ( g1 resource short name or str(list of short names),
          g2 resource short name or str(list of short names),
          human-readable discrepancy reason )
      Either or both of the g1 and g2 image resource short names may be None if
      it's not applicable for the discrepancy reason.
    """
    discrepancies = []
    g1_image_to_info = g1._ExtractImages()
    g2_image_to_info = g2._ExtractImages()
    for image in set(g1_image_to_info.keys()) - set(g2_image_to_info.keys()):
      discrepancies.append((image, None, 'Missing in g2'))
    for image in set(g2_image_to_info.keys()) - set(g1_image_to_info.keys()):
      discrepancies.append((None, image, 'Missing in g1'))

    for image in set(g1_image_to_info.keys()) & set(g2_image_to_info.keys()):
      def PredecessorInfo(g, n):
        info = [g._ShortName(p) for p in n.Node().Predecessors()]
        info.sort()
        return str(info)
      g1_pred = PredecessorInfo(g1, g1_image_to_info[image])
      g2_pred = PredecessorInfo(g2, g2_image_to_info[image])
      if g1_pred != g2_pred:
        discrepancies.append((g1_pred, g2_pred,
                              'Predecessor mismatch for ' + image))

    return discrepancies

  def Set(self, cache_all=None, node_filter=None):
    """Set model parameters.

    TODO(mattcary): add parameters for caching certain types of resources (just
    scripts, just cacheable, etc).

    Args:
      cache_all: boolean that if true ignores empirical resource load times for
        all resources.
      node_filter: a Node->boolean used to restrict the graph for most
        operations.
    """
    if self._cache_all is not None:
      self._cache_all = cache_all
    if node_filter is not None:
      self._node_filter = node_filter

  def Nodes(self, sort=False):
    """Return iterable of all nodes via their NodeInfos.

    Args:
      sort: if true, return nodes in sorted order. This may prune additional
        nodes from the unsorted list (eg, non-root, non-ad nodes reachable only
        by ad nodes)

    Returns:
      Iterable of node infos.

    """
    if sort:
      return (self._node_info[n.Index()]
              for n in dag.TopologicalSort(self._nodes, self._node_filter))
    return (n for n in self._node_info if self._node_filter(n.Node()))

  def EdgeCosts(self, node_filter=None):
    """Edge costs.

    Args:
      node_filter: if not none, a Node->boolean filter to use instead of the
      current one from Set.

    Returns:
      The total edge costs of our graph.

    """
    node_filter = self._node_filter if node_filter is None else node_filter
    total = 0
    for n in self._node_info:
      if not node_filter(n.Node()):
        continue
      for s in n.Node().Successors():
        if node_filter(s):
          total += self.EdgeCost(n.Node(), s)
    return total

  def Intersect(self, other_nodes):
    """Return iterable of nodes that intersect with another graph.

    Args:
      other_nodes: iterable of the nodes of another graph, eg from Nodes().

    Returns:
      an iterable of (mine, other) pairs for all nodes for which the URL is
      identical.
    """
    other_map = {n.Url(): n for n in other_nodes}
    for n in self._node_info:
      if self._node_filter(n.Node()) and n.Url() in other_map:
        yield(n, other_map[n.Url()])

  def Cost(self, path_list=None, costs_out=None):
    """Compute cost of current model.

    Args:
      path_list: if not None, gets a list of NodeInfo in the longest path.
      costs_out: if not None, gets a vector of node costs by node index. Any
        filtered nodes will have zero cost.

    Returns:
      Cost of the longest path.

    """
    costs = [0] * len(self._nodes)
    for n in dag.TopologicalSort(self._nodes, self._node_filter):
      cost = 0
      if n.Predecessors():
        cost = max([costs[p.Index()] + self.EdgeCost(p, n)
                    for p in n.Predecessors()])
      if not self._cache_all:
        cost += self.NodeCost(n)
      costs[n.Index()] = cost
    max_cost = max(costs)
    if costs_out is not None:
      del costs_out[:]
      costs_out.extend(costs)
    assert max_cost > 0  # Otherwise probably the filter went awry.
    if path_list is not None:
      del path_list[:]
      n = (i for i in self._nodes if costs[i.Index()] == max_cost).next()
      path_list.append(self._node_info[n.Index()])
      while n.Predecessors():
        n = reduce(lambda costliest, next:
                   next if (self._node_filter(next) and
                            costs[next.Index()] > costs[costliest.Index()])
                        else costliest,
                   n.Predecessors())
        path_list.insert(0, self._node_info[n.Index()])
    return max_cost

  def FilterAds(self, node):
    """A filter for use in eg, Cost, to remove advertising nodes.

    Args:
      node: A dag.Node.

    Returns:
      True if the node is not ad-related.
    """
    node_info = self._node_info[node.Index()]
    return not (node_info.IsAd() or node_info.IsTracking())

  def ResourceInfo(self):
    """Get resource info.

    Returns:
      A list of NodeInfo objects that describe the resources fetched.
    """
    return [n for n in self._node_info if n.Request() is not None]

  def DebugString(self):
    """Graph structure for debugging.

    TODO(mattcary): this fails for graphs with more than one component or where
    self._nodes[0] is not a root.

    Returns:
      A human-readable string of the graph.
    """
    output = []
    queue = [self._nodes[0]]
    visited = set()
    while queue:
      n = queue.pop(0)
      assert n not in visited
      visited.add(n)
      children = n.SortedSuccessors()
      output.append('%d -> [%s]' %
                    (n.Index(), ' '.join([str(c.Index()) for c in children])))
      for c in children:
        assert n in c.Predecessors()  # Integrity checking
        queue.append(c)
    assert len(visited) == len(self._nodes)
    return '\n'.join(output)

  def NodeInfo(self, node):
    """Return the node info for a graph node.

    Args:
      node: (int, dag.Node or NodeInfo) a node representation. An int is taken
      to be the node's index.

    Returns:
      The NodeInfo instance corresponding to the node.
    """
    if type(node) is self._NodeInfo:
      return node
    elif type(node) is int:
      return self._node_info[node]
    return self._node_info[node.Index()]

  def ShortName(self, node):
    """Convenience function for redirecting to NodeInfo."""
    return self.NodeInfo(node).ShortName()

  def Url(self, node):
    """Convenience function for redirecting to NodeInfo."""
    return self.NodeInfo(node).Url()

  def NodeCost(self, node):
    """Convenience function for redirecting to NodeInfo."""
    return self.NodeInfo(node).NodeCost()

  def EdgeCost(self, parent, child):
    """Convenience function for redirecting to NodeInfo."""
    return self.NodeInfo(parent).EdgeCost(self.NodeInfo(child))

  def EdgeAnnotations(self, parent, child):
    """Convenience function for redirecting to NodeInfo."""
    return self.NodeInfo(parent).EdgeAnnotations(self.NodeInfo(child))

  ##
  ## Internal items
  ##

  # This resource type may induce a timing dependency. See _SplitChildrenByTime
  # for details.
  # TODO(mattcary): are these right?
  _CAN_BE_TIMING_PARENT = set(['script', 'magic-debug-content'])
  _CAN_MAKE_TIMING_DEPENDENCE = set(['json', 'other', 'magic-debug-content'])

  class _NodeInfo(object):
    """Our internal class that adds cost and other information to nodes.

    Costs are stored on the node as well as edges. Edge information is only
    stored on successor edges and not predecessor, that is, you get them from
    the parent and not the child.

    We also store the request on the node, and expose request-derived
    information like content type.
    """
    def __init__(self, node, request):
      """Create a new node info.

      Args:
        node: The node to augment.
        request: The request associated with this node, or an (index, msec)
          tuple.
      """
      self._node = node
      self._is_ad = False
      self._is_tracking = False
      self._edge_costs = {}
      self._edge_annotations = {}

      if type(request) == tuple:
        self._request = None
        self._node_cost = 0
        self._shortname = 'LOAD %s' % request[0]
        self._start_time = request[1]
      else:
        self._shortname = None
        self._start_time = None
        self._request = request
        # All fields in timing are millis relative to request_time.
        self._node_cost = max(
            [0] + [t for f, t in request.timing._asdict().iteritems()
                   if f != 'request_time'])

    def __str__(self):
      return self.ShortName()

    def Node(self):
      return self._node

    def Index(self):
      return self._node.Index()

    def SetRequestContent(self, is_ad, is_tracking):
      """Sets the kind of content the request relates to.

      Args:
        is_ad: (bool) Whether the request is an Ad.
        is_tracking: (bool) Whether the request is related to tracking.
      """
      (self._is_ad, self._is_tracking) = (is_ad, is_tracking)

    def IsAd(self):
      return self._is_ad

    def IsTracking(self):
      return self._is_tracking

    def Request(self):
      return self._request

    def NodeCost(self):
      return self._node_cost

    def EdgeCost(self, s):
      return self._edge_costs.get(s, 0)

    def StartTime(self):
      if self._start_time:
        return self._start_time
      return self._request.timing.request_time * 1000

    def EndTime(self):
      return self.StartTime() + self._node_cost

    def EdgeAnnotations(self, s):
      assert s.Node() in self.Node().Successors()
      return self._edge_annotations.get(s, {})

    def ContentType(self):
      if self._request is None:
        return 'synthetic'
      return self._request.GetContentType()

    def ShortName(self):
      """Returns either the hostname of the resource, or the filename,
      or the end of the path. Tries to include the domain as much as possible.
      """
      if self._shortname:
        return self._shortname
      parsed = urlparse.urlparse(self._request.url)
      path = parsed.path
      hostname = parsed.hostname if parsed.hostname else '?.?.?'
      if path != '' and path != '/':
        last_path = parsed.path.split('/')[-1]
        if len(last_path) < 10:
          if len(path) < 10:
            return hostname + '/' + path
          else:
            return hostname + '/..' + parsed.path[-10:]
        elif len(last_path) > 10:
          return hostname + '/..' + last_path[:5]
        else:
          return hostname + '/..' + last_path
      else:
        return hostname

    def Url(self):
      return self._request.url

    def SetEdgeCost(self, child, cost):
      assert child.Node() in self._node.Successors()
      self._edge_costs[child] = cost

    def AddEdgeAnnotations(self, s, annotations):
      assert s.Node() in self._node.Successors()
      self._edge_annotations.setdefault(s, {}).update(annotations)

    def ReparentTo(self, old_parent, new_parent):
      """Move costs and annotatations from old_parent to new_parent.

      Also updates the underlying node connections, ie, do not call
      old_parent.RemoveSuccessor(), etc.

      Args:
        old_parent: the NodeInfo of a current parent of self. We assert this
          is actually a parent.
        new_parent: the NodeInfo of the new parent. We assert it is not already
          a parent.
      """
      assert old_parent.Node() in self.Node().Predecessors()
      assert new_parent.Node() not in self.Node().Predecessors()
      edge_annotations = old_parent._edge_annotations.pop(self, {})
      edge_cost =  old_parent._edge_costs.pop(self)
      old_parent.Node().RemoveSuccessor(self.Node())
      new_parent.Node().AddSuccessor(self.Node())
      new_parent.SetEdgeCost(self, edge_cost)
      new_parent.AddEdgeAnnotations(self, edge_annotations)

    def __eq__(self, o):
      """Note this works whether o is a Node or a NodeInfo."""
      return self.Index() == o.Index()

    def __hash__(self):
      return hash(self.Node().Index())

  def _BuildDag(self, trace):
    """Build DAG of resources.

    Build a DAG from our requests and augment with NodeInfo (see above) in a
    parallel array indexed by Node.Index().

    Creates self._nodes and self._node_info.

    Args:
      trace: A LoadingTrace.
    """
    self._nodes = []
    self._node_info = []
    index_by_request = {}
    for request in trace.request_track.GetEvents():
      next_index = len(self._nodes)
      assert request not in index_by_request
      index_by_request[request] = next_index
      node = dag.Node(next_index)
      node_info = self._NodeInfo(node, request)
      if self._content_lens:
        node_info.SetRequestContent(
            self._content_lens.IsAdRequest(request),
            self._content_lens.IsTrackingRequest(request))
      self._nodes.append(node)
      self._node_info.append(node_info)

    dependencies = self.REQUEST_LENS(trace).GetRequestDependencies()
    for dep in dependencies:
      (parent_rq, child_rq, reason) = dep
      parent = self._node_info[index_by_request[parent_rq]]
      child = self._node_info[index_by_request[child_rq]]
      edge_cost = request_track.TimeBetween(parent_rq, child_rq, reason)
      if edge_cost < 0:
        edge_cost = 0
        if child.StartTime() < parent.StartTime():
          logging.error('Inverted dependency: %s->%s',
                        parent.ShortName(), child.ShortName())
          # Note that child.StartTime() < parent.EndTime() appears to happen a
          # fair amount in practice.
      parent.Node().AddSuccessor(child.Node())
      parent.SetEdgeCost(child, edge_cost)
      parent.AddEdgeAnnotations(child, {self.EDGE_KIND_KEY: reason})
      if self._activity_lens:
        activity = self._activity_lens.BreakdownEdgeActivityByInitiator(dep)
        parent.AddEdgeAnnotations(child, {'activity': activity})

    self._AugmentFrameLoads(index_by_request)

  def _AugmentFrameLoads(self, index_by_request):
    if not self._frame_lens:
      return
    loads = self._frame_lens.GetFrameLoadInfo()
    load_index_to_node = {}
    for l in loads:
      next_index = len(self._nodes)
      node = dag.Node(next_index)
      node_info = self._NodeInfo(node, (l.index, l.msec))
      load_index_to_node[l.index] = next_index
      self._nodes.append(node)
      self._node_info.append(node_info)
    frame_deps = self._frame_lens.GetFrameLoadDependencies()
    for load_idx, rq in frame_deps[0]:
      parent = self._node_info[load_index_to_node[load_idx]]
      child = self._node_info[index_by_request[rq]]
      parent.Node().AddSuccessor(child.Node())
      parent.AddEdgeAnnotations(child, {self.EDGE_KIND_KEY: 'after-load'})
    for rq, load_idx in frame_deps[1]:
      child = self._node_info[load_index_to_node[load_idx]]
      parent = self._node_info[index_by_request[rq]]
      parent.Node().AddSuccessor(child.Node())
      parent.AddEdgeAnnotations(child, {self.EDGE_KIND_KEY: 'before-load'})

  def _SplitChildrenByTime(self, parent):
    """Split children of a node by request times.

    The initiator of a request may not be the true dependency of a request. For
    example, a script may appear to load several resources independently, but in
    fact one of them may be a JSON data file, and the remaining resources assets
    described in the JSON. The assets should be dependent upon the JSON data
    file, and not the original script.

    This function approximates that by rearranging the children of a node
    according to their request times. The predecessor of each child is made to
    be the node with the greatest finishing time, that is before the start time
    of the child.

    We do this by sorting the nodes twice, once by start time and once by end
    time. We mark the earliest end time, and then we walk the start time list,
    advancing the end time mark when it is less than our current start time.

    This is refined by only considering assets which we believe actually create
    a dependency. We only split if the original parent is a script, and the new
    parent a data file. We confirm these relationships heuristically by loading
    pages multiple times and ensuring that dependencies do not change; see
    CheckImageLoadConsistency() for details.

    We incorporate this heuristic by skipping over any non-script/json resources
    when moving the end mark.

    TODO(mattcary): More heuristics, like incorporating cachability somehow, and
    not just picking arbitrarily if there are two nodes with the same end time
    (does that ever really happen?)

    Args:
      parent: the NodeInfo whose children we are going to rearrange.

    """
    if parent.ContentType() not in self._CAN_BE_TIMING_PARENT:
      return  # No dependency changes.
    children_by_start_time = [self._node_info[s.Index()]
                              for s in parent.Node().Successors()]
    children_by_start_time.sort(key=lambda c: c.StartTime())
    children_by_end_time = [self._node_info[s.Index()]
                            for s in parent.Node().Successors()]
    children_by_end_time.sort(key=lambda c: c.EndTime())
    end_mark = 0
    for current in children_by_start_time:
      if current.StartTime() < parent.EndTime() - 1e-5:
        logging.warning('Child loaded before parent finished: %s -> %s',
                        parent.ShortName(), current.ShortName())
      go_to_next_child = False
      while end_mark < len(children_by_end_time):
        if children_by_end_time[end_mark] == current:
          go_to_next_child = True
          break
        elif (children_by_end_time[end_mark].ContentType() not in
            self._CAN_MAKE_TIMING_DEPENDENCE):
          end_mark += 1
        elif (end_mark < len(children_by_end_time) - 1 and
              children_by_end_time[end_mark + 1].EndTime() <
                  current.StartTime()):
          end_mark += 1
        else:
          break
      if end_mark >= len(children_by_end_time):
        break  # It's not possible to rearrange any more children.
      if go_to_next_child:
        continue  # We can't rearrange this child, but the next child may be
                  # eligible.
      if children_by_end_time[end_mark].EndTime() <= current.StartTime():
        current.ReparentTo(parent, children_by_end_time[end_mark])
        children_by_end_time[end_mark].AddEdgeAnnotations(
            current, {self.EDGE_KIND_KEY: 'timing'})

  def _ExtractImages(self):
    """Return interesting image resources.

    Uninteresting image resources are things like ads that we don't expect to be
    constant across fetches.

    Returns:
      Dict of image url + short name to NodeInfo.
    """
    image_to_info = {}
    for n in self._node_info:
      if (n.ContentType() is not None and
          n.ContentType().startswith('image') and
          self.FilterAds(n)):
        key = str((n.Url(), n.ShortName(), n.StartTime()))
        assert key not in image_to_info, n.Url()
        image_to_info[key] = n
    return image_to_info
