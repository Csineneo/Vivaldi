# Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved

from __future__ import print_function

import sys, os
import argparse

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__),
                            "..", "..", "chromium", "tools", "grit")))

from grit import grd_reader
#from grit.node import base
from grit.node import empty
from grit.node import include
from grit.node import structure
from grit.node import message
from grit.node import node_io
from grit.node import misc
from grit import util
from grit import tclib

def getMessage(msg):
  bits = []
  for part in msg.parts:
    if isinstance(part, tclib.Placeholder):
      # Skip this since translators look at msgid to see the pattern for msgstr
      #if isMsgId:
      #  bits.append('<ph name="'+part.presentation+'">'+part.original+'<ex>'+part.example+'</ex></ph>');
      #else:
      bits.append('<ph name="'+part.presentation+'" />');
    else:
      bits.append(part)
  r = ''.join(bits).replace('"', '\\"') # escape quotes
  r = '\\n'.join(r.splitlines())        # escape newlines
  return r.encode("utf8")

generated_translation = set()

def write_message(f, options, node, locale=None):
  translated = ""
  if options.locale:
    try:
      translated = getMessage(node.clique.MessageForLanguage(locale,
                                       node.PseudoIsAllowed(),
                                       node.ShouldFallbackToEnglish(),
                                       missing_translation_is_error = True))
    except:
      return
  name = node.attrs["name"]
  message = getMessage(node.clique.GetMessage())
  clique = node.GetCliques()[0]
  tid = clique.GetId()

  unique = (name, message)
  if unique in generated_translation:
    return
  generated_translation.add(unique)

  print("#. Description:", node.attrs["desc"].encode("utf8"), file=f)
  print("#. TranslationId:", tid, file=f)
  print("#. vivaldi-file:", options.vivaldi_file, file=f)

  print('#, fuzzy', file=f)
  print('msgctxt "%s"' % name, file=f)
  print('msgid "%s"' % message, file=f)
  print('msgstr "%s"' % translated, file=f)
  print(file=f)

def main():

  argparser = argparse.ArgumentParser();
  argparser.add_argument("--chromium-file")
  argparser.add_argument("--vivaldi-file")
  argparser.add_argument("--locale")
  argparser.add_argument("--messages")
  argparser.add_argument("-D", action="append", dest="defines", default=[])
  # grit build also supports '-E KEY=VALUE', support that to share command
  # line flags. Dummy parameter in this script
  argparser.add_argument("-E", action="append", dest="build_env", default=[])
  argparser.add_argument("grd_file")
  argparser.add_argument("output_file")

  options = argparser.parse_args()

  defines = {}
  for define in options.defines:
    name, val = util.ParseDefine(define)
    defines[name] = val

  node_all = {}
  node_list = {}
  for platform in ["win32", "linux3", "darwin", "android"]:
    resources = grd_reader.Parse(options.grd_file, defines = defines, target_platform=platform)
    resources.SetOutputLanguage('en')
    resources.RunGatherers()

    id_counter = 0
    for node in resources:
      id_counter += 1
      node.unique_id = id_counter
      if node.unique_id not in node_all:
        node_all[node.unique_id] = node
      elif node_all[node.unique_id].attrs != node.attrs:
        raise Exception("foo")

    message_list = None
    if options.messages:
      with open(options.messages, "r") as f:
        message_list=[x.strip() for x in f.readlines()]

    for node in resources.ActiveDescendants():
      if node.unique_id not in node_list:
        if (not isinstance(node, message.MessageNode) or
            not node.IsTranslateable() or
            "name" not in node.attrs):
          continue
        if message_list and node.attrs["name"] not in message_list:
          continue
        node_list[node.unique_id] = node

  with open(options.output_file, "w") as f:
    print('msgid ""', file=f)
    print('msgstr ""', file=f)
    if options.locale:
      print('"Language: %s\\n"' % options.locale, file=f)
    print('"MIME-Version: 1.0\\n"', file=f)
    print('"Content-Type: text/plain; charset=UTF-8\\n"', file=f)
    print('', file=f)
    for _, node in sorted(node_list.items()):
      write_message(f, options, node, options.locale);

if __name__ == "__main__":
  main()
