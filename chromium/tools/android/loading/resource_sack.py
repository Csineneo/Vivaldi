# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A collection of ResourceGraphs.

Processes multiple ResourceGraphs, all presumably from requests to the same
site. Common urls are collected in Bags and different statistics on the
relationship between bags are collected.
"""

import collections
import json
import sys
import urlparse

from collections import defaultdict

import content_classification_lens
import dag
import user_satisfied_lens

class GraphSack(object):
  """Aggreate of ResourceGraphs.

  Collects ResourceGraph nodes into bags, where each bag contains the nodes with
  common urls. Dependency edges are tracked between bags (so that each bag may
  be considered as a node of a graph). This graph of bags is referred to as a
  sack.

  Each bag is associated with a dag.Node, even though the bag graph may not be a
  DAG. The edges are annotated with list of graphs and nodes that generated
  them.
  """
  _GraphInfo = collections.namedtuple('_GraphInfo', (
      'cost',   # The graph cost (aka critical path length).
      'total_costs',  # A vector by node index of total cost of each node.
      ))

  def __init__(self):
    # A bag is a node in our combined graph.
    self._bags = []
    # Each bag in our sack corresponds to a url, as expressed by this map.
    self._url_to_bag = {}
    # Maps graph -> _GraphInfo structures for each graph we've consumed.
    self._graph_info = {}

  def ConsumeGraph(self, graph):
    """Add a graph and process.

    Args:
      graph: (ResourceGraph) the graph to add. The graph is processed sorted
        according to its current filter.
    """
    assert graph not in self._graph_info
    critical_path = []
    total_costs = []
    cost = graph.Cost(path_list=critical_path,
                      costs_out=total_costs)
    self._graph_info[graph] = self._GraphInfo(
        cost=cost, total_costs=total_costs)
    for n in graph.Nodes(sort=True):
      assert graph._node_filter(n.Node())
      self.AddNode(graph, n)
    for node in critical_path:
      self._url_to_bag[node.Url()].MarkCritical()

  def AddNode(self, graph, node):
    """Add a node to our collection.

    Args:
      graph: (ResourceGraph) the graph in which the node lives.
      node: (NodeInfo) the node to add.

    Returns:
      The Bag containing the node.
    """
    if not graph._node_filter(node):
      return
    if node.Url() not in self._url_to_bag:
      new_index = len(self._bags)
      self._bags.append(Bag(self, new_index, node.Url()))
      self._url_to_bag[node.Url()] = self._bags[-1]
    self._url_to_bag[node.Url()].AddNode(graph, node)
    return self._url_to_bag[node.Url()]

  @property
  def graph_info(self):
    return self._graph_info

  @property
  def bags(self):
    return self._bags

class Bag(dag.Node):
  def __init__(self, sack, index, url):
    super(Bag, self).__init__(index)
    self._sack = sack
    self._url = url
    self._label = self._MakeShortname(url)
    # Maps a ResourceGraph to its Nodes contained in this Bag.
    self._graphs = defaultdict(set)
    # Maps each successor bag to the set of (graph, node, graph-successor)
    # tuples that generated it.
    self._successor_sources = defaultdict(set)
    # Maps each successor bag to a set of edge costs. This is just used to
    # track min and max; if we want more statistics we'd have to count the
    # costs with multiplicity.
    self._successor_edge_costs = defaultdict(set)

    # Miscellaneous counts and costs used in display.
    self._total_costs = []
    self._relative_costs = []
    self._num_critical = 0

  @property
  def url(self):
    return self._url

  @property
  def label(self):
    return self._label

  @property
  def graphs(self):
    return self._graphs

  @property
  def successor_sources(self):
    return self._successor_sources

  @property
  def successor_edge_costs(self):
    return self._successor_edge_costs

  @property
  def total_costs(self):
    return self._total_costs

  @property
  def relative_costs(self):
    return self._relative_costs

  @property
  def num_critical(self):
    return self._num_critical

  @property
  def num_nodes(self):
    return len(self._total_costs)

  def MarkCritical(self):
    self._num_critical += 1

  def AddNode(self, graph, node):
    if node in self._graphs[graph]:
      return  # Already added.
    graph_info = self._sack.graph_info[graph]
    self._graphs[graph].add(node)
    node_total_cost = graph_info.total_costs[node.Index()]
    self._total_costs.append(node_total_cost)
    self._relative_costs.append(
        float(node_total_cost) / graph_info.cost)
    for s in node.Node().Successors():
      if not graph._node_filter(s):
        continue
      node_info = graph.NodeInfo(s)
      successor_bag = self._sack.AddNode(graph, node_info)
      self.AddSuccessor(successor_bag)
      self._successor_sources[successor_bag].add((graph, node, s))
      self._successor_edge_costs[successor_bag].add(graph.EdgeCost(node, s))

  @classmethod
  def _MakeShortname(cls, url):
    parsed = urlparse.urlparse(url)
    if parsed.scheme == 'data':
      kind, _ = parsed.path.split(';', 1)
      return 'data:' + kind
    path = parsed.path[:10]
    hostname = parsed.hostname if parsed.hostname else '?.?.?'
    return hostname + '/' + path
