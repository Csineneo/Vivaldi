// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Tests for cr-action-menu element. Runs as an interactive UI
 * test, since many of these tests check focus behavior.
 */
suite('CrActionMenu', function() {
  /** @type {?CrActionMenuElement} */
  var menu = null;

  /** @type {?NodeList<HTMLElement>} */
  var items = null;

  setup(function() {
    PolymerTest.clearBody();

    document.body.innerHTML = `
      <button id="dots">...</button>
      <dialog is="cr-action-menu">
        <button class="dropdown-item">Un</button>
        <hr>
        <button class="dropdown-item">Dos</button>
        <button class="dropdown-item">Tres</button>
      </dialog>
    `;

    menu = document.querySelector('dialog[is=cr-action-menu]');
    items = menu.querySelectorAll('.dropdown-item');
    assertEquals(3, items.length);
  });

  teardown(function() {
    if (menu.open)
      menu.close();
  });

  function down() {
    MockInteractions.keyDownOn(menu, 'ArrowDown', [], 'ArrowDown');
  }

  function up() {
    MockInteractions.keyDownOn(menu, 'ArrowUp', [], 'ArrowUp');
  }

  test('hidden or disabled items', function() {
    menu.showAt(document.querySelector('#dots'));
    down();
    assertEquals(menu.root.activeElement, items[0]);

    menu.close();
    items[0].hidden = true;
    menu.showAt(document.querySelector('#dots'));
    down();
    assertEquals(menu.root.activeElement, items[1]);

    menu.close();
    items[1].disabled = true;
    menu.showAt(document.querySelector('#dots'));
    down();
    assertEquals(menu.root.activeElement, items[2]);
  });

  test('focus after down/up arrow', function() {
    menu.showAt(document.querySelector('#dots'));

    // The menu should be focused when shown, but not on any of the items.
    assertEquals(menu, document.activeElement);
    assertNotEquals(items[0], menu.root.activeElement);
    assertNotEquals(items[1], menu.root.activeElement);
    assertNotEquals(items[2], menu.root.activeElement);

    down();
    assertEquals(items[0], menu.root.activeElement);
    down();
    assertEquals(items[1], menu.root.activeElement);
    down();
    assertEquals(items[2], menu.root.activeElement);
    down();
    assertEquals(items[0], menu.root.activeElement);
    up();
    assertEquals(items[2], menu.root.activeElement);
    up();
    assertEquals(items[1], menu.root.activeElement);
    up();
    assertEquals(items[0], menu.root.activeElement);
    up();
    assertEquals(items[2], menu.root.activeElement);

    items[1].disabled = true;
    up();
    assertEquals(items[0], menu.root.activeElement);
  });

  test('pressing up arrow when no focus will focus last item', function(){
    menu.showAt(document.querySelector('#dots'));
    assertEquals(menu, document.activeElement);

    up();
    assertEquals(items[items.length - 1], menu.root.activeElement);
  });

  test('close on resize', function() {
    menu.showAt(document.querySelector('#dots'));
    assertTrue(menu.open);

    window.dispatchEvent(new CustomEvent('resize'));
    assertFalse(menu.open);
  });

  test('close on popstate', function() {
    menu.showAt(document.querySelector('#dots'));
    assertTrue(menu.open);

    window.dispatchEvent(new CustomEvent('popstate'));
    assertFalse(menu.open);
  });

  /** @param {string} key The key to use for closing. */
  function testFocusAfterClosing(key) {
    return new Promise(function(resolve) {
      var dots = document.querySelector('#dots');
      menu.showAt(dots);
      assertTrue(menu.open);

      // Check that focus returns to the anchor element.
      dots.addEventListener('focus', resolve);
      MockInteractions.keyDownOn(menu, key, [], key);
      assertFalse(menu.open);
    });
  }

  test('close on Tab', function() { return testFocusAfterClosing('Tab'); });
  test('close on Escape', function() {
    return testFocusAfterClosing('Escape');
  });

  test('mouse movement focus options', function() {
    function makeMouseoverEvent(node) {
      var e = new MouseEvent('mouseover', {bubbles: true});
      node.dispatchEvent(e);
    }

    menu.showAt(document.querySelector('#dots'));

    // Moving mouse on option 1 should focus it.
    assertNotEquals(items[0], menu.root.activeElement);
    makeMouseoverEvent(items[0]);
    assertEquals(items[0], menu.root.activeElement);

    // Moving mouse on the menu (not on option) should focus the menu.
    makeMouseoverEvent(menu);
    assertNotEquals(items[0], menu.root.activeElement);
    assertEquals(menu, document.activeElement);

    // Mouse movements should override keyboard focus.
    down();
    down();
    assertEquals(items[1], menu.root.activeElement);
    makeMouseoverEvent(items[0]);
    assertEquals(items[0], menu.root.activeElement);
  });
});
