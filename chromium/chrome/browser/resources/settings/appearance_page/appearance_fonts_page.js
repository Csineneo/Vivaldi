// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {
  'use strict';

  /**
   * This is the absolute difference maintained between standard and
   * fixed-width font sizes. http://crbug.com/91922.
   * @const @private {number}
   */
  var SIZE_DIFFERENCE_FIXED_STANDARD_ = 3;

  /** @const @private {!Array<number>} */
  var FONT_SIZE_RANGE_ = [
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36,
    40, 44, 48, 56, 64, 72,
  ];

  /** @const @private {!Array<number>} */
  var MINIMUM_FONT_SIZE_RANGE_ = [
    6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24
  ];

  /**
   * 'settings-appearance-fonts-page' is the settings page containing appearance
   * settings.
   *
   * Example:
   *
   *   <settings-appearance-fonts-page prefs="{{prefs}}">
   *   </settings-appearance-fonts-page>
   *
   * @group Chrome Settings Elements
   * @element settings-appearance-fonts-page
   */
  Polymer({
    is: 'settings-appearance-fonts-page',

    properties: {
      /**
       * The font size used by default.
       * @private
       */
      defaultFontSize_: {
        type: Number,
      },

      /**
       * The value of the font size slider.
       * @private
       */
      fontSizeIndex_: {
        type: Number,
      },

      /**
       * Common font sizes.
       * @private {!Array<number>}
       */
      fontSizeRange_: {
        readOnly: true,
        type: Array,
        value: FONT_SIZE_RANGE_,
      },

      /**
       * Upper bound of the font size slider.
       * @private
       */
      fontSizeRangeLimit_: {
        readOnly: true,
        type: Number,
        value: FONT_SIZE_RANGE_.length - 1,
      },

      /**
       * The interactive value of the minimum font size slider.
       * @private
       */
      immediateMinimumSizeIndex_: {
        type: Number,
      },

      /**
       * The interactive value of the font size slider.
       * @private
       */
      immediateSizeIndex_: {
        type: Number,
      },

      /**
       * Reasonable, minimum font sizes.
       * @private {!Array<number>}
       */
      minimumFontSizeRange_: {
        readOnly: true,
        type: Array,
        value: MINIMUM_FONT_SIZE_RANGE_,
      },

      /**
       * Upper bound of the minimum font size slider.
       * @private
       */
      minimumFontSizeRangeLimit_: {
        readOnly: true,
        type: Number,
        value: MINIMUM_FONT_SIZE_RANGE_.length - 1,
      },

      /**
       * The font size used at minimum.
       * @private
       */
      minimumFontSize_: {
        type: Number,
      },

      /**
       * The value of the minimum font size slider.
       * @private
       */
      minimumSizeIndex_: {
        type: Number,
      },

      /**
       * Preferences state.
       */
      prefs: {
        type: Object,
        notify: true,
      },
    },

    observers: [
      'fontSizeChanged_(prefs.webkit.webprefs.default_font_size.value)',
      'minimumFontSizeChanged_(prefs.webkit.webprefs.minimum_font_size.value)',
    ],

    /** @override */
    ready: function() {
      var self = this;
      cr.define('Settings', function() {
        return {
          setFontsData: function() {
            return self.setFontsData_.apply(self, arguments);
          },
        };
      });
      chrome.send('fetchFontsData');
    },

    /**
     * @param {number} value The intermediate slider value.
     * @private
     */
    immediateSizeIndexChanged_: function(value) {
      this.set('prefs.webkit.webprefs.default_font_size.value',
          this.fontSizeRange_[this.immediateSizeIndex_]);
    },

    /**
     * @param {number} value The intermediate slider value.
     * @private
     */
    immediateMinimumSizeIndexChanged_: function(value) {
      this.set('prefs.webkit.webprefs.minimum_font_size.value',
          this.minimumFontSizeRange_[this.immediateMinimumSizeIndex_]);
    },

    /**
     * @param {!Array<{0: string, 1: (string|undefined),
     *     2: (string|undefined)}>}
     *   fontList The font menu options.
     * @param {!Array<{0: string, 1: string}>} encodingList The encoding menu
     *   options.
     * @private
     */
    setFontsData_: function(fontList, encodingList) {
      var fontMenuOptions = [];
      for (var i = 0; i < fontList.length; ++i)
        fontMenuOptions.push({value: fontList[i][0], name: fontList[i][1]});
      this.$.standardFont.menuOptions = fontMenuOptions;
      this.$.serifFont.menuOptions = fontMenuOptions;
      this.$.sansSerifFont.menuOptions = fontMenuOptions;
      this.$.fixedFont.menuOptions = fontMenuOptions;

      var encodingMenuOptions = [];
      for (var i = 0; i < encodingList.length; ++i) {
        encodingMenuOptions.push({
            value: encodingList[i][0], name: encodingList[i][1]});
      }
      this.$.encoding.menuOptions = encodingMenuOptions;
    },

    /**
     * @param {number} value The changed font size slider value.
     * @private
     */
    fontSizeChanged_: function(value) {
      this.defaultFontSize_ = value;
      if (!this.$.sizeSlider.dragging) {
        this.fontSizeIndex_ = this.fontSizeRange_.indexOf(value);
        this.set('prefs.webkit.webprefs.default_fixed_font_size.value',
          value - SIZE_DIFFERENCE_FIXED_STANDARD_);
      }
    },

    /**
     * @param {number} value The changed font size slider value.
     * @private
     */
    minimumFontSizeChanged_: function(value) {
      this.minimumFontSize_ = value;
      if (!this.$.minimumSizeSlider.dragging)
        this.minimumSizeIndex_ = this.minimumFontSizeRange_.indexOf(value);
    },

    /**
     * Creates an html style value.
     * @param {number} fontSize The font size to use.
     * @param {string} fontFamily The name of the font family use.
     * @return {string}
     * @private
     */
    computeStyle_: function(fontSize, fontFamily) {
      return 'font-size: ' + fontSize + "px; font-family: '" + fontFamily +
          "';";
    },
  });
})();
