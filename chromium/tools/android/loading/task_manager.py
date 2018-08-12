# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""API that build and execute recipes wrapped into a task dependency graph.

A Task consists of a 'recipe' (a closure to be executed) and a list of refs to
tasks that should be executed prior to executing this Task (i.e. dependencies).

A Task can be either 'static' or 'dynamic'. A static tasks only represents an
existing file on the filesystem, its recipe is a no-op. The responsibility of
the recipe of a dynamic task is to produce the file with the name assigned at
task creation.

A scenario is a ordered list of dynamic tasks to execute such that the
dependencies of a given task are execute before the said task. The scenario is
built from a list of final tasks and a list of frozen tasks:
  - A final task is a task to execute ultimately. Therefore the scenario is
    composed of final tasks and their required intermediary tasks.
  - A frozen task is dynamic task to not execute. This is a mechanism to morph a
    dynamic task that may have dependencies to a static task with no dependency
    at scenario generation time, injecting what the dynamic task have already
    produced before as an input of the smaller tasks dependency graph covered
    by the scenario.

Example:
  # -------------------------------------------------- Build my dependency graph
  builder = Builder('my/output/dir')
  input0 = builder.CreateStaticTask('input0', 'path/to/input/file0')
  input1 = builder.CreateStaticTask('input1', 'path/to/input/file1')
  input2 = builder.CreateStaticTask('input2', 'path/to/input/file2')
  input3 = builder.CreateStaticTask('input3', 'path/to/input/file3')

  @builder.RegisterTask('out0', dependencies=[input0, input2])
  def BuildOut0():
    DoStuff(input0.path, input2.path, out=BuildOut0.path)

  @builder.RegisterTask('out1', dependencies=[input1, input3])
  def BuildOut1():
    DoStuff(input1.path, input3.path, out=BuildOut1.path)

  @builder.RegisterTask('out2', dependencies=[BuildOut0, BuildOut1])
  def BuildOut2():
    DoStuff(BuildOut0.path, BuildOut1.path, out=BuildOut2.path)

  @builder.RegisterTask('out3', dependencies=[BuildOut0])
  def BuildOut3():
    DoStuff(BuildOut0.path, out=BuildOut3.path)

  # ---------------------------- Case 1: Execute BuildOut3 and its dependencies.
  for task in GenerateScenario(final_tasks=[BuildOut3], frozen_tasks=[])
    task.Execute()

  # ---------- Case 2: Execute BuildOut2 and its dependencies but not BuildOut1.
  # It is required that BuildOut1.path is already existing.
  for task in GenerateScenario(final_tasks=[BuildOut2],
                               frozen_tasks=[BuildOut1])
    task.Execute()
"""


import argparse
import logging
import os
import subprocess
import sys

import common_util


_TASK_GRAPH_DOTFILE_NAME = 'tasks_graph.dot'
_TASK_GRAPH_PNG_NAME = 'tasks_graph.png'


class TaskError(Exception):
  pass


class Task(object):
  """Task that can be either a static task or dynamic with a recipe."""

  def __init__(self, name, path, dependencies, recipe):
    """Constructor.

    Args:
      name: The name of the  task.
      path: Path to the file or directory that this task produces.
      dependencies: List of parent task to execute before.
      recipe: Function to execute if a dynamic task or None if a static task.
    """
    self.name = name
    self.path = path
    self._dependencies = dependencies
    self._recipe = recipe
    self._is_done = recipe == None

  def Execute(self):
    """Executes this task."""
    if self.IsStatic():
      raise TaskError('Task {} is static.'.format(self.name))
    if not self._is_done:
      self._recipe()
    self._is_done = True

  def IsStatic(self):
    """Returns whether this task is a static task."""
    return self._recipe == None


class Builder(object):
  """Utilities for creating sub-graphs of tasks with dependencies."""

  def __init__(self, output_directory):
    """Constructor.

    Args:
      output_directory: Output directory where the dynamic tasks work.
    """
    self.output_directory = output_directory
    self.tasks = {}

  def CreateStaticTask(self, task_name, path):
    if not os.path.exists(path):
      raise TaskError('Error while creating task {}: File not found: {}'.format(
          task_name, path))
    if task_name in self.tasks:
      raise TaskError('Task {} already exists.'.format(task_name))
    task = Task(task_name, path, [], None)
    self.tasks[task_name] = task
    return task

  # Caution:
  #   This decorator may not create a dynamic task in the case where
  #   merge=True and another dynamic target having the same name have already
  #   been created. In this case, it will just reuse the former task. This is at
  #   the user responsibility to ensure that merged tasks would do the exact
  #   same thing.
  #
  #     @builder.RegisterTask('hello')
  #     def TaskA():
  #       my_object.a = 1
  #
  #     @builder.RegisterTask('hello', merge=True)
  #     def TaskB():
  #       # This function won't be executed ever.
  #       my_object.a = 2 # <------- Wrong because different from what TaskA do.
  #
  #     assert TaskA == TaskB
  #     TaskB.Execute() # Sets set my_object.a == 1
  def RegisterTask(self, task_name, dependencies=None, merge=False):
    """Decorator that wraps a function into a dynamic task.

    Args:
      task_name: The name of this new task to register.
      dependencies: List of SandwichTarget to build before this task.
      merge: If a task already have this name, don't create a new one and
        reuse the existing one.

    Returns:
      A Task that was created by wrapping the function or an existing registered
      wrapper (that have wrapped a different function).
    """
    dependencies = dependencies or []
    def InnerAddTaskWithNewPath(recipe):
      if task_name in self.tasks:
        if not merge:
          raise TaskError('Task {} already exists.'.format(task_name))
        task = self.tasks[task_name]
        if task.IsStatic():
          raise TaskError('Should not merge dynamic task {} with the already '
                          'existing static one.'.format(task_name))
        return task
      task_path = os.path.join(self.output_directory, task_name)
      task = Task(task_name, task_path, dependencies, recipe)
      self.tasks[task_name] = task
      return task
    return InnerAddTaskWithNewPath


def GenerateScenario(final_tasks, frozen_tasks):
  """Generates a list of tasks to execute in order of dependencies-first.

  Args:
    final_tasks: The final tasks to generate the scenario from.
    frozen_tasks: Sets of task to freeze.

  Returns:
    [Task]
  """
  scenario = []
  task_paths = {}
  def InternalAppendTarget(task):
    if task.IsStatic():
      return
    if task in frozen_tasks:
      if not os.path.exists(task.path):
        raise TaskError('Frozen target `{}`\'s path doesn\'t exist.'.format(
            task.name))
      return
    if task.path in task_paths:
      if task_paths[task.path] == None:
        raise TaskError('Target `{}` depends on itself.'.format(task.name))
      if task_paths[task.path] != task:
        raise TaskError(
            'Tasks `{}` and `{}` produce the same file: `{}`.'.format(
                task.name, task_paths[task.path].name, task.path))
      return
    task_paths[task.path] = None
    for dependency in task._dependencies:
      InternalAppendTarget(dependency)
    task_paths[task.path] = task
    scenario.append(task)

  for final_task in final_tasks:
    InternalAppendTarget(final_task)
  return scenario


def ListResumingTasksToFreeze(scenario, final_tasks, failed_task):
  """Lists the tasks that one needs to freeze to be able to resume the scenario
  after failure.

  Args:
    scenario: The scenario (list of Task) to be resumed.
    final_tasks: The list of final Task used to generate the scenario.
    failed_task: A Task that have failed in the scenario.

  Returns:
    set(Task)
  """
  task_to_id = {t: i for i, t in enumerate(scenario)}
  assert failed_task in task_to_id
  frozen_tasks = set()
  walked_tasks = set()

  def InternalWalk(task):
    if task.IsStatic() or task in walked_tasks:
      return
    walked_tasks.add(task)
    if task not in task_to_id:
      frozen_tasks.add(task)
    elif task_to_id[task] < task_to_id[failed_task]:
      frozen_tasks.add(task)
    else:
      for dependency in task._dependencies:
        InternalWalk(dependency)

  for final_task in final_tasks:
    InternalWalk(final_task)
  return frozen_tasks


def OutputGraphViz(scenario, final_tasks, output):
  """Outputs the build dependency graph covered by this scenario.

  Args:
    scenario: The generated scenario.
    final_tasks: The final tasks used to generate the scenario.
    output: A file-like output stream to receive the dot file.

  Graph interpretations:
    - Static tasks are shape less.
    - Final tasks (the one that where directly appended) are box shaped.
    - Non final dynamic tasks are ellipse shaped.
    - Frozen dynamic tasks have a blue shape.
  """
  task_execution_ids = {t: i for i, t in enumerate(scenario)}
  tasks_node_ids = dict()

  def GetTaskNodeId(task):
    if task in tasks_node_ids:
      return tasks_node_ids[task]
    node_id = len(tasks_node_ids)
    node_label = task.name
    node_color = 'blue'
    node_shape = 'ellipse'
    if task.IsStatic():
      node_shape = 'plaintext'
    elif task in task_execution_ids:
      node_color = 'black'
      node_label = str(task_execution_ids[task]) + ': ' + node_label
    if task in final_tasks:
      node_shape = 'box'
    output.write('  n{} [label="{}", color={}, shape={}];\n'.format(
        node_id, node_label, node_color, node_shape))
    tasks_node_ids[task] = node_id
    return node_id

  output.write('digraph graphname {\n')
  for task in scenario:
    task_node_id = GetTaskNodeId(task)
    for dep in task._dependencies:
      dep_node_id = GetTaskNodeId(dep)
      output.write('  n{} -> n{};\n'.format(dep_node_id, task_node_id))
  output.write('}\n')


def CommandLineParser():
  """Creates command line arguments parser meant to be used as a parent parser
  for any entry point that use the ExecuteWithCommandLine() function.

  Returns:
    The command line arguments parser.
  """
  parser = argparse.ArgumentParser(add_help=False)
  parser.add_argument('-d', '--dry-run', action='store_true',
                      help='Only prints the deps of tasks to build.')
  parser.add_argument('-e', '--to-execute', metavar='REGEX', type=str,
                      nargs='+', dest='run_regexes', default=[],
                      help='Regex selecting tasks to execute.')
  parser.add_argument('-f', '--to-freeze', metavar='REGEX', type=str,
                      nargs='+', dest='frozen_regexes', default=[],
                      help='Regex selecting tasks to not execute.')
  parser.add_argument('-o', '--output', type=str, required=True,
                      help='Path of the output directory.')
  parser.add_argument('-v', '--output-graphviz', action='store_true',
      help='Outputs the {} and {} file in the output directory.'
           ''.format(_TASK_GRAPH_DOTFILE_NAME, _TASK_GRAPH_PNG_NAME))
  return parser


def _GetCommandLineArgumentsStr(final_task_regexes, frozen_tasks):
  arguments = []
  if frozen_tasks:
    arguments.append('-f')
    arguments.extend([task.name for task in frozen_tasks])
  if final_task_regexes:
    arguments.append('-e')
    arguments.extend(final_task_regexes)
  return subprocess.list2cmdline(arguments)


def ExecuteWithCommandLine(args, tasks, default_final_tasks):
  """Helper to execute tasks using command line arguments.

  Args:
    args: Command line argument parsed with CommandLineParser().
    tasks: Unordered list of tasks to publish to command line regexes.
    default_final_tasks: Default final tasks if there is no -r command
      line arguments.

  Returns:
    0 if success or 1 otherwise
  """
  frozen_regexes = [common_util.VerboseCompileRegexOrAbort(e)
                      for e in args.frozen_regexes]
  run_regexes = [common_util.VerboseCompileRegexOrAbort(e)
                   for e in args.run_regexes]

  # Lists frozen tasks
  frozen_tasks = set()
  if frozen_regexes:
    for task in tasks:
      for regex in frozen_regexes:
        if regex.search(task.name):
          frozen_tasks.add(task)
          break

  # Lists final tasks.
  final_tasks = default_final_tasks
  if run_regexes:
    final_tasks = []
    for task in tasks:
      for regex in run_regexes:
        if regex.search(task.name):
          final_tasks.append(task)
          break

  # Create the scenario.
  scenario = GenerateScenario(final_tasks, frozen_tasks)

  if len(scenario) == 0:
    logging.error('No tasks to build.')
    return 1

  if not os.path.isdir(args.output):
    os.makedirs(args.output)

  # Print the task dependency graph visualization.
  if args.output_graphviz:
    graphviz_path = os.path.join(args.output, _TASK_GRAPH_DOTFILE_NAME)
    png_graph_path = os.path.join(args.output, _TASK_GRAPH_PNG_NAME)
    with open(graphviz_path, 'w') as output:
      OutputGraphViz(scenario, final_tasks, output)
    subprocess.check_call(['dot', '-Tpng', graphviz_path, '-o', png_graph_path])

  # Use the build scenario.
  if args.dry_run:
    for task in scenario:
      print '{}:{}'.format(
          task.name, ' '.join([' \\\n  ' + d.name for d in task._dependencies]))
  else:
    for task in scenario:
      logging.info('%s %s' % ('-' * 60, task.name))
      try:
        task.Execute()
      except:
        print '# Looks like something went wrong in \'{}\''.format(task.name)
        print '#'
        print '# To re-execute only this task, add the following parameters:'
        print '#   ' + _GetCommandLineArgumentsStr(
            [task.name], task._dependencies)
        print '#'
        print '# To resume from this task, add the following parameters:'
        print '#   ' + _GetCommandLineArgumentsStr(args.run_regexes,
            ListResumingTasksToFreeze(scenario, final_tasks, task))
        raise
  return 0
