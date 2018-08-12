// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Replace the current body of the test with a new element.
 * @param {Element} element
 */
function replaceBody(element) {
  PolymerTest.clearBody();
  document.body.appendChild(element);
}

/**
 * Initialize a tree for UI testing. This performs the same initialization as
 * `setUpStore_` in <bookmarks-store>, but without the need for a store element
 * in the test.
 * @param {BookmarkTreeNode} rootNode
 */
function setupTreeForUITests(rootNode){
  if (!rootNode.path)
    rootNode.path = 'rootNode';

  BookmarksStore.generatePaths(rootNode, 0);
  BookmarksStore.initNodes(rootNode);
}

/**
 * Creates a folder with given properties.
 * @param {string} id
 * @param {Array<BookmarkTreeNode>} children
 * @param {Object=} config
 * @return {BookmarkTreeNode}
 */
function createFolder(id, children, config) {
  var newFolder = {
    id: id,
    children: children,
    title: '',
  };
  if (config) {
    for (var key in config)
      newFolder[key] = config[key];
  }
  if (children.length) {
    for (var i = 0; i < children.length; i++) {
      children[i].index = i;
      children[i].parentId = newFolder.id;
    }
  }
  return newFolder;
}

/**
 * Creates a bookmark with given properties.
 * @param {string} id
 * @param {Object=} config
 * @return {BookmarkTreeNode}
 */
function createItem(id, config) {
  var newItem = {
    id: id,
    title: '',
    url: 'http://www.google.com/',
  };
  if (config) {
    for (var key in config)
      newItem[key] = config[key];
  }
  return newItem;
}
