# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Models for loading in chrome.

(Redirect the following to the general model module once we have one)
A model is an object with the following methods.
  CostMs(): return the cost of the cost in milliseconds.
  Set(): set model-specifical parameters.

ResourceGraph
  This creates a DAG of resource dependancies from loading.log_requests to model
  loading time. The model may be parameterized by changing the loading time of
  a particular or all resources.
"""

import logging
import os
import urlparse
import sys

import dag
import log_parser

class ResourceGraph(object):
  """A model of loading by a DAG (tree?) of resource dependancies.

  Set parameters:
    cache_all: if true, assume zero loading time for all resources.
  """

  def __init__(self, requests):
    """Create from a parsed request set.

    Args:
      requests: [RequestData, ...] filtered RequestData from loading.log_parser.
    """
    self._BuildDag(requests)
    self._global_start = min([n.StartTime() for n in self._node_info])
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
    """Check that images have the same dependancies between ResourceGraphs.

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
    scripts, just cachable, etc).

    Args:
      cache_all: boolean that if true ignores emperical resource load times for
        all resources.
      node_filter: a Node->boolean used to restrict the graph for most
        operations.
    """
    if self._cache_all is not None:
      self._cache_all = cache_all
    if node_filter is not None:
      self._node_filter = node_filter

  def Nodes(self):
    """Return iterable of all nodes via their _NodeInfos.

    Returns:
      Iterable of node infos in arbitrary order.
    """
    for n in self._node_info:
      if self._node_filter(n.Node()):
        yield n

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
          total += self._EdgeCost(n.Node(), s)
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


  def Cost(self, path_list=None):
    """Compute cost of current model.

    Args:
      path_list: if not None, gets a list of NodeInfo in the longest path.

    Returns:
      Cost of the longest path.

    """
    costs = [0] * len(self._nodes)
    for n in dag.TopologicalSort(self._nodes, self._node_filter):
      cost = 0
      if n.Predecessors():
        cost = max([costs[p.Index()] + self._EdgeCost(p, n)
                    for p in n.Predecessors()])
      if not self._cache_all:
        cost += self._NodeCost(n)
      costs[n.Index()] = cost
    max_cost = max(costs)
    assert max_cost > 0  # Otherwise probably the filter went awry.
    if path_list is not None:
      del path_list[:-1]
      n = (i for i in self._nodes if costs[i.Index()] == max_cost).next()
      path_list.append(self._node_info[n.Index()])
      while n.Predecessors():
        n = reduce(lambda costliest, next:
                   next if (self._node_filter(next) and
                            cost[next.Index()] > cost[costliest.Index()])
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
    return not self._IsAdUrl(self._node_info[node.Index()].Url())

  def MakeGraphviz(self, output, highlight=None):
    """Output a graphviz representation of our DAG.

    Args:
      output: a file-like output stream which recieves a graphviz dot.
      highlight: a list of node items to emphasize. Any resource url which
        contains any highlight text will be distinguished in the output.
    """
    output.write("""digraph dependencies {
    rankdir = LR;
    """)
    orphans = set()
    try:
      sorted_nodes = dag.TopologicalSort(self._nodes,
                                         node_filter=self._node_filter)
    except AssertionError as exc:
      sys.stderr.write('Bad topological sort: %s\n'
                       'Writing children in order\n' % str(exc))
      sorted_nodes = self._nodes
    for n in sorted_nodes:
      if not n.Successors() and not n.Predecessors():
        orphans.add(n)
    if orphans:
      output.write("""subgraph cluster_orphans {
  color=black;
  label="Orphans";
""")
      for n in orphans:
        output.write(self._GraphvizNode(n.Index(), highlight))
      output.write('}\n')

    output.write("""subgraph cluster_nodes {
  color=invis;
""")
    for n in sorted_nodes:
      if not n.Successors() and not n.Predecessors():
        continue
      output.write(self._GraphvizNode(n.Index(), highlight))

    for n in sorted_nodes:
      for s in n.Successors():
        style = 'color = orange'
        annotations = self._EdgeAnnotation(n, s)
        if 'parser' in annotations:
          style = 'color = red'
        elif 'stack' in annotations:
          style = 'color = blue'
        elif 'script_inferred' in annotations:
          style = 'color = purple'
        if 'timing' in annotations:
          style += '; style=dashed'
        arrow = '[%s; label="%s"]' % (style, self._EdgeCost(n, s))
        output.write('%d -> %d %s;\n' % (n.Index(), s.Index(), arrow))
    output.write('}\n}\n')

  def ResourceInfo(self):
    """Get resource info.

    Returns:
      A list of _NodeInfo objects that describe the resources fetched.
    """
    return self._node_info

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

  ##
  ## Internal items
  ##

  _CONTENT_TYPE_TO_COLOR = {'html': 'red', 'css': 'green', 'script': 'blue',
                            'json': 'purple', 'gif_image': 'grey',
                            'image': 'orange', 'other': 'white'}

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
        request: The request associated with this node.
      """
      self._request = request
      self._node = node
      self._edge_costs = {}
      self._edge_annotations = {}
      # All fields in timing are millis relative to requestTime, which is epoch
      # seconds.
      self._node_cost = max([t for f, t in request.timing._asdict().iteritems()
                             if f != 'requestTime'])

    def __str__(self):
      return self.ShortName()

    def Node(self):
      return self._node

    def Index(self):
      return self._node.Index()

    def Request(self):
      return self._request

    def NodeCost(self):
      return self._node_cost

    def EdgeCost(self, s):
      return self._edge_costs[s]

    def StartTime(self):
      return self._request.timing.requestTime * 1000

    def EndTime(self):
      return self._request.timing.requestTime * 1000 + self._node_cost

    def EdgeAnnotation(self, s):
      assert s.Node() in self.Node().Successors()
      return self._edge_annotations.get(s, [])

    def ContentType(self):
      return log_parser.Resource.FromRequest(self._request).GetContentType()

    def ShortName(self):
      return log_parser.Resource.FromRequest(self._request).GetShortName()

    def Url(self):
      return self._request.url

    def SetEdgeCost(self, child, cost):
      assert child.Node() in self._node.Successors()
      self._edge_costs[child] = cost

    def AddEdgeAnnotation(self, s, annotation):
      assert s.Node() in self._node.Successors()
      self._edge_annotations.setdefault(s, []).append(annotation)

    def ReparentTo(self, old_parent, new_parent):
      """Move costs and annotatations from old_parent to new_parent.

      Also updates the underlying node connections, ie, do not call
      old_parent.RemoveSuccessor(), etc.

      Args:
        old_parent: the _NodeInfo of a current parent of self. We assert this
          is actually a parent.
        new_parent: the _NodeInfo of the new parent. We assert it is not already
          a parent.
      """
      assert old_parent.Node() in self.Node().Predecessors()
      assert new_parent.Node() not in self.Node().Predecessors()
      edge_annotations = old_parent._edge_annotations.pop(self, [])
      edge_cost =  old_parent._edge_costs.pop(self)
      old_parent.Node().RemoveSuccessor(self.Node())
      new_parent.Node().AddSuccessor(self.Node())
      new_parent.SetEdgeCost(self, edge_cost)
      for a in edge_annotations:
        new_parent.AddEdgeAnnotation(self, a)

    def __eq__(self, o):
      return self.Node().Index() == o.Node().Index()

    def __hash__(self):
      return hash(self.Node().Index())

  def _ShortName(self, node):
    """Convenience function for redirecting Nodes to _NodeInfo."""
    return self._node_info[node.Index()].ShortName()

  def _Url(self, node):
    """Convenience function for redirecting Nodes to _NodeInfo."""
    return self._node_info[node.Index()].Url()

  def _NodeCost(self, node):
    """Convenience function for redirecting Nodes to _NodeInfo."""
    return self._node_info[node.Index()].NodeCost()

  def _EdgeCost(self, parent, child):
    """Convenience function for redirecting Nodes to _NodeInfo."""
    return self._node_info[parent.Index()].EdgeCost(
        self._node_info[child.Index()])

  def _EdgeAnnotation(self, parent, child):
    """Convenience function for redirecting Nodes to _NodeInfo."""
    return self._node_info[parent.Index()].EdgeAnnotation(
        self._node_info[child.Index()])

  def _BuildDag(self, requests):
    """Build DAG of resources.

    Build a DAG from our requests and augment with _NodeInfo (see above) in a
    parallel array indexed by Node.Index().

    Creates self._nodes and self._node_info.

    Args:
      requests: [Request, ...] Requests from loading.log_parser.
    """
    self._nodes = []
    self._node_info = []
    indicies_by_url = {}
    requests_by_completion = log_parser.SortedByCompletion(requests)
    for request in requests:
      next_index = len(self._nodes)
      indicies_by_url.setdefault(request.url, []).append(next_index)
      node = dag.Node(next_index)
      node_info = self._NodeInfo(node, request)
      self._nodes.append(node)
      self._node_info.append(node_info)
    for url, indicies in indicies_by_url.iteritems():
      if len(indicies) > 1:
        logging.warning('Multiple loads (%d) for url: %s' %
                        (len(indicies), url))
    for i in xrange(len(requests)):
      request = requests[i]
      current_node_info = self._node_info[i]
      resource = log_parser.Resource.FromRequest(current_node_info.Request())
      initiator = request.initiator
      initiator_type = initiator['type']
      predecessor_url = None
      predecessor_type = None
      # Classify & infer the predecessor. If a candidate url we identify as the
      # predecessor is not in index_by_url, then we haven't seen it in our
      # requests and we will try to find a better predecessor.
      if initiator_type == 'parser':
        url = initiator['url']
        if url in indicies_by_url:
          predecessor_url = url
          predecessor_type = 'parser'
      elif initiator_type == 'script' and 'stackTrace' in initiator:
        for frame in initiator['stackTrace']:
          url = frame['url']
          if url in indicies_by_url:
            predecessor_url = url
            predecessor_type = 'stack'
            break
      elif initiator_type == 'script':
        # When the initiator is a script without a stackTrace, infer that it
        # comes from the most recent script from the same hostname.  TLD+1 might
        # be better, but finding what is a TLD requires a database.
        request_hostname = urlparse.urlparse(request.url).hostname
        sorted_script_requests_from_hostname = [
            r for r in requests_by_completion
            if (resource.GetContentType() in ('script', 'html', 'json')
                and urlparse.urlparse(r.url).hostname == request_hostname)]
        most_recent = None
        # Linear search is bad, but this shouldn't matter here.
        for r in sorted_script_requests_from_hostname:
          if r.timestamp < request.timing.requestTime:
            most_recent = r
          else:
            break
        if most_recent is not None:
          url = most_recent.url
          if url in indicies_by_url:
            predecessor_url = url
            predecessor_type = 'script_inferred'
      # TODO(mattcary): we skip initiator type other, is that correct?
      if predecessor_url is not None:
        predecessor = self._FindBestPredecessor(
            current_node_info, indicies_by_url[predecessor_url])
        edge_cost = current_node_info.StartTime() - predecessor.EndTime()
        if edge_cost < 0:
          edge_cost = 0
        if current_node_info.StartTime() < predecessor.StartTime():
          logging.error('Inverted dependency: %s->%s',
                        predecessor.ShortName(), current_node_info.ShortName())
          # Note that current.StartTime() < predecessor.EndTime() appears to
          # happen a fair amount in practice.
        predecessor.Node().AddSuccessor(current_node_info.Node())
        predecessor.SetEdgeCost(current_node_info, edge_cost)
        predecessor.AddEdgeAnnotation(current_node_info, predecessor_type)

  def _FindBestPredecessor(self, node_info, candidate_indicies):
    """Find best predecessor for node_info

    If there is only one candidate, we use it regardless of timings. We will
    later warn about inverted dependencies. If there are more than one, we use
    the latest whose end time is before node_info's start time. If there is no
    such candidate, we throw up our hands and return an arbitrary one.

    Args:
      node_info: _NodeInfo of interest.
      candidate_indicies: indicies of candidate predecessors.

    Returns:
      _NodeInfo of best predecessor.
    """
    assert candidate_indicies
    if len(candidate_indicies) == 1:
      return self._node_info[candidate_indicies[0]]
    candidate = self._node_info[candidate_indicies[0]]
    for i in xrange(1, len(candidate_indicies)):
      next_candidate = self._node_info[candidate_indicies[i]]
      if (next_candidate.EndTime() < node_info.StartTime() and
          next_candidate.StartTime() > candidate.StartTime()):
        candidate = next_candidate
    if candidate.EndTime() > node_info.StartTime():
      logging.warning('Multiple candidates but all inverted for ' +
                      node_info.ShortName())
    return candidate


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
    pages multiple times and ensuring that dependacies do not change; see
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
        children_by_end_time[end_mark].AddEdgeAnnotation(current, 'timing')

  def _GraphvizNode(self, index, highlight):
    """Returns a graphviz node description for a given node.

    Args:
      index: index of the node.
      highlight: a list of node items to emphasize. Any resource url which
        contains any highlight text will be distinguished in the output.

    Returns:
      A string describing the resource in graphviz format.
      The resource is color-coded according to its content type, and its shape
      is oval if its max-age is less than 300s (or if it's not cacheable).
    """
    node_info = self._node_info[index]
    color = self._CONTENT_TYPE_TO_COLOR[node_info.ContentType()]
    max_age = log_parser.MaxAge(node_info.Request())
    shape = 'polygon' if max_age > 300 else 'oval'
    styles = ['filled']
    if highlight:
      for fragment in highlight:
        if fragment in node_info.Url():
          styles.append('dotted')
          break
    return ('%d [label = "%s\\n%.2f->%.2f (%.2f)"; style = "%s"; '
            'fillcolor = %s; shape = %s];\n'
            % (index, node_info.ShortName(),
               node_info.StartTime() - self._global_start,
               node_info.EndTime() - self._global_start,
               node_info.EndTime() - node_info.StartTime(),
               ','.join(styles), color, shape))

  @classmethod
  def _IsAdUrl(cls, url):
    """Return true if the url is an ad.

    We group content that doesn't seem to be specific to the website along with
    ads, eg staticxx.facebook.com, as well as analytics like googletagmanager (?
    is this correct?).

    Args:
      url: The full string url to examine.

    Returns:
      True iff the url appears to be an ad.

    """
    # See below for how these patterns are defined.
    AD_PATTERNS = ['2mdn.net',
                   'admarvel.com',
                   'adnxs.com',
                   'adobedtm.com',
                   'adsrvr.org',
                   'adsafeprotected.com',
                   'adsymptotic.com',
                   'adtech.de',
                   'adtechus.com',
                   'advertising.com',
                   'atwola.com',  # brand protection from cscglobal.com?
                   'bounceexchange.com',
                   'betrad.com',
                   'casalemedia.com',
                   'cloudfront.net//test.png',
                   'cloudfront.net//atrk.js',
                   'contextweb.com',
                   'crwdcntrl.net',
                   'doubleclick.net',
                   'dynamicyield.com',
                   'krxd.net',
                   'facebook.com//ping',
                   'fastclick.net',
                   'google.com//-ads.js',
                   'cse.google.com',  # Custom search engine.
                   'googleadservices.com',
                   'googlesyndication.com',
                   'googletagmanager.com',
                   'lightboxcdn.com',
                   'mediaplex.com',
                   'meltdsp.com',
                   'mobile.nytimes.com//ads-success',
                   'mookie1.com',
                   'newrelic.com',
                   'nr-data.net',   # Apparently part of newrelic.
                   'optnmnstr.com',
                   'pubmatic.com',
                   'quantcast.com',
                   'quantserve.com',
                   'rubiconproject.com',
                   'scorecardresearch.com',
                   'sekindo.com',
                   'serving-sys.com',
                   'sharethrough.com',
                   'staticxx.facebook.com',  # ?
                   'syndication.twimg.com',
                   'tapad.com',
                   'yieldmo.com',
                ]
    parts = urlparse.urlparse(url)
    for pattern in AD_PATTERNS:
      if '//' in pattern:
        domain, path = pattern.split('//')
      else:
        domain, path = (pattern, None)
      if parts.netloc.endswith(domain):
        if not path or path in parts.path:
          return True
    return False

  def _ExtractImages(self):
    """Return interesting image resources.

    Uninteresting image resources are things like ads that we don't expect to be
    constant across fetches.

    Returns:
      Dict of image url + short name to NodeInfo.
    """
    image_to_info = {}
    for n in self._node_info:
      if (n.ContentType().startswith('image') and
          not self._IsAdUrl(n.Url())):
        key = str((n.Url(), n.ShortName(), n.StartTime()))
        assert key not in image_to_info, n.Url()
        image_to_info[key] = n
    return image_to_info
