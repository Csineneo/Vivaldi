// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This Polymer element is used as a header for the media router interface.
Polymer({
  is: 'media-router-header',

  properties: {
    /**
     * The name of the icon used as the back button. This is set once, when
     * the |this| is ready.
     * @private {string}
     */
    arrowDropIcon_: {
      type: String,
      value: '',
    },

    /**
     * Whether or not the arrow drop icon should be disabled.
     * @type {boolean}
     */
    arrowDropIconDisabled: {
      type: Boolean,
      value: false,
    },

    /**
     * The header text to show.
     * @type {string}
     */
    headingText: {
      type: String,
      value: '',
    },

    /**
     * The height of the header when it shows the user email.
     * @private {number}
     */
    headerWithEmailHeight_: {
      type: Number,
      readOnly: true,
      value: 62,
    },

    /**
     * Whether to show the user email in the header.
     * @type {boolean}
     */
    showEmail: {
      type: Boolean,
      value: false,
      observer: 'maybeChangeHeaderHeight_',
    },

    /**
     * The text to show in the tooltip.
     * @type {string}
     */
    tooltip: {
      type: String,
      value: '',
    },

    /**
     * The user email if they are signed in.
     * @type {string}
     */
    userEmail: {
      type: String,
      value: '',
    },

    /**
     * The current view that this header should reflect.
     * @type {?media_router.MediaRouterView}
     */
    view: {
      type: String,
      value: null,
      observer: 'updateHeaderCursorStyle_',
    },
  },

  listeners: {
    'focus': 'onFocus_',
  },

  ready: function() {
    // If this is not on a Mac platform, remove the placeholder. See
    // onFocus_() for more details. ready() is only called once, so no need
    // to check if the placeholder exist before removing.
    if (!cr.isMac)
      this.$$('#focus-placeholder').remove();
  },

  attached: function() {
    // isRTL() only works after i18n_template.js runs to set <html dir>.
    // Set the back button icon based on text direction.
    this.arrowDropIcon_ = isRTL() ? 'arrow-forward' : 'arrow-back';
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @return {string} The current arrow-drop-* icon to use.
   * @private
   */
  computeArrowDropIcon_: function(view) {
    return view == media_router.MediaRouterView.CAST_MODE_LIST ?
        'arrow-drop-up' : 'arrow-drop-down';
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @return {boolean} Whether or not the arrow drop icon should be hidden.
   * @private
   */
  computeArrowDropIconHidden_: function(view) {
    return view != media_router.MediaRouterView.SINK_LIST &&
        view != media_router.MediaRouterView.CAST_MODE_LIST;
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @return {boolean} Whether or not the back button should be hidden.
   * @private
   */
  computeBackButtonHidden_: function(view) {
    return view != media_router.MediaRouterView.ROUTE_DETAILS &&
        view != media_router.MediaRouterView.FILTER;
  },

  /**
   * Returns whether given string is undefined, null, empty, or whitespace only.
   * @param {?string} str String to be tested.
   * @return {boolean} |true| if the string is undefined, null, empty, or
   *     whitespace.
   * @private
   */
  isEmptyOrWhitespace_: function(str) {
    return str === undefined || str === null || (/^\s*$/).test(str);
  },

  /**
   * Handles a click on the back button by firing a back-click event.
   *
   * @private
   */
  onBackButtonClick_: function() {
    this.fire('back-click');
  },

  /**
   * Handles a click on the close button by firing a close-button-click event.
   *
   * @private
   */
  onCloseButtonClick_: function() {
    this.fire('close-dialog', {
      pressEscToClose: false,
    });
  },

  /**
   * Called when a focus event is triggered.
   *
   * @param {!Event} event The event object.
   * @private
   */
  onFocus_: function(event) {
    // If the focus event was not triggered by the user, remove focus from
    // the element. This prevents unexpected focusing when the dialog is
    // initially loaded.
    // This only happens on mac.
    if (cr.isMac && !event.sourceCapabilities) {
      event.path[0].blur();
      // Adding a focus placeholder element is part of the workaround for
      // handling unexpected focusing, which only happens once on dialog open.
      // Since #focus-placeholder initially is focus-enabled, as denoted by
      // its tabindex value, the focus will not appear in other elements.
      // Remove the placeholder since we have no more use for it.
      this.$$('#focus-placeholder').remove();
    }
  },

  /**
   * Handles a click on the arrow button by firing an arrow-click event.
   *
   * @private
   */
  onHeaderOrArrowClick_: function() {
    if (this.view == media_router.MediaRouterView.SINK_LIST ||
        this.view == media_router.MediaRouterView.CAST_MODE_LIST) {
      this.fire('header-or-arrow-click');
    }
  },

  /**
   * Updates header height to accomodate email text. This is called on changes
   * to |showEmail| and will return early if the value has not changed.
   *
   * @param {boolean} oldValue .
   * @param {boolean} newValue .
   * @private
   */
  maybeChangeHeaderHeight_: function(oldValue, newValue) {
    if (!!oldValue == !!newValue) {
      return;
    }

    // Ensures conditional templates are stamped.
    this.async(function() {
      var currentHeight = this.offsetHeight;

      this.$$('#header-toolbar').style.height =
          this.showEmail && !this.isEmptyOrWhitespace_(this.userEmail) ?
              this.headerWithEmailHeight_ + 'px' : undefined;

      // Only fire if height actually changed.
      if (currentHeight != this.offsetHeight) {
        this.fire('header-height-changed');
      }
    });
  },

  /**
   * Updates the cursor style for the header text when the view changes. When
   * the drop arrow is also shown, the header text is also clickable.
   *
   * @param {?media_router.MediaRouterView} view The current view.
   * @private
   */
  updateHeaderCursorStyle_: function(view) {
    this.$$('#header-text').style.cursor =
        view == media_router.MediaRouterView.SINK_LIST ||
        view == media_router.MediaRouterView.CAST_MODE_LIST ?
            'pointer' : 'auto';
  },
});
