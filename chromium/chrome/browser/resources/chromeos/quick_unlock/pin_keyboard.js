// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'pin-keyboard' is a keyboard that can be used to enter PINs or more generally
 * numeric values.
 *
 * Properties:
 *    value: The value of the PIN keyboard. Writing to this property will adjust
 *           the PIN keyboard's value.
 *
 * Events:
 *    pin-change: Fired when the PIN value has changed. The pin is available at
 *                event.detail.pin.
 *    submit: Fired when the PIN is submitted. The pin is available at
 *            event.detail.pin.
 *
 * Example:
 *    <pin-keyboard on-pin-change="onPinChange" on-submit="onPinSubmit"
 *                  value="{{pinValue}}">
 *    </pin-keyboard>
 */
Polymer({
  is: 'pin-keyboard',

  properties: {
    /** The value stored in the keyboard's input element. */
    value: {
      type: String,
      notify: true,
      value: '',
      observer: 'onPinValueChange_'
    }
  },

  /** Transfers focus to the input element. */
  focus: function() {
    this.$$('#pin-input').focus();
  },

  /** Called when a keypad number has been tapped. */
  onNumberTap_: function(event, detail) {
    var numberValue = event.target.getAttribute('value');
    this.value += numberValue;
  },

  /** Fires a submit event with the current PIN value. */
  firePinSubmitEvent_: function() {
    this.fire('submit', { pin: this.value });
  },

  /**
   * Fires an update event with the current PIN value. The event will only be
   * fired if the PIN value has actually changed.
   * @param {string} value
   * @param {string} previous
   */
  onPinValueChange_: function(value, previous) {
    if (value != previous)
      this.fire('pin-change', { pin: value });
  },

  /** Called when the user wants to erase the last character of the entered
   *  PIN value.
   */
  onPinClear_: function() {
    this.value = this.value.substring(0, this.value.length - 1);
  },

  /** Called when a key event is pressed while the input element has focus. */
  onInputKeyDown_: function(event) {
    // Up/down pressed, swallow the event to prevent the input value from
    // being incremented or decremented.
    if (event.keyCode == 38 || event.keyCode == 40) {
      event.preventDefault();
      return;
    }

    // Enter pressed.
    if (event.keyCode == 13) {
      this.firePinSubmitEvent_();
      event.preventDefault();
      return;
    }
  }
});
