// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This Polymer element contains the entire media router interface. It handles
// hiding and showing specific components.
Polymer({
  is: 'media-router-container',

  properties: {
    /**
     * The list of available sinks.
     * @type {!Array<!media_router.Sink>}
     */
    allSinks: {
      type: Array,
      value: [],
      observer: 'reindexSinksAndRebuildSinksToShow_',
    },

    /**
     * The list of CastModes to show.
     * @type {!Array<!media_router.CastMode>}
     */
    castModeList: {
      type: Array,
      value: [],
      observer: 'checkCurrentCastMode_',
    },

    /**
     * The ID of the Sink currently being launched.
     * @private {string}
     */
    currentLaunchingSinkId_: {
      type: String,
      value: '',
    },

    /**
     * The current route.
     * @private {?media_router.Route}
     */
    currentRoute_: {
      type: Object,
      value: null,
    },

    /**
     * The current view to be shown.
     * @private {?media_router.MediaRouterView}
     */
    currentView_: {
      type: String,
      value: null,
      observer: 'currentViewChanged_',
    },

    /**
     * The text for when there are no devices.
     * @private {string}
     */
    deviceMissingText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('deviceMissing');
      },
    },

    /**
     * The URL to open when the device missing link is clicked.
     * @type {string}
     */
    deviceMissingUrl: {
      type: String,
      value: '',
    },

    /**
     * The height of the dialog.
     * @private {number}
     */
    dialogHeight_: {
      type: Number,
      value: 330,
    },

    /**
     * The time |this| element calls ready().
     * @private {number}
     */
    elementReadyTimeMs_: {
      type: Number,
      value: 0,
    },

    /**
     * The text for the first run flow button.
     * @private {string}
     */
    firstRunFlowButtonText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('firstRunFlowButton');
      },
    },

    /**
     * The text for the learn more link about cloud services in the first run
     * flow.
     * @private {string}
     */
    firstRunFlowLearnMore_: {
      type: String,
      readOnly: true,
      value: loadTimeData.getString('learnMoreText'),
    },

    /**
     * The URL to open when the cloud services pref learn more link is clicked.
     * @type {string}
     */
    firstRunFlowCloudPrefLearnMoreUrl: {
      type: String,
      value: '',
    },

    /**
     * The text for the cloud services preference description in the first run
     * flow.
     * @private {string}
     */
    firstRunFlowCloudPrefText_: {
      type: String,
      readOnly: true,
      value: loadTimeData.valueExists('firstRunFlowCloudPrefText') ?
          loadTimeData.getString('firstRunFlowCloudPrefText') : '',
    },

    /**
     * The URL to open when the first run flow learn more link is clicked.
     * @type {string}
     */
    firstRunFlowLearnMoreUrl: {
      type: String,
      value: '',
    },

    /**
     * The text description for the first run flow.
     * @private {string}
     */
    firstRunFlowText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('firstRunFlowText');
      },
    },

    /**
     * The header of the first run flow.
     * @private {string}
     */
    firstRunFlowTitle_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('firstRunFlowTitle');
      },
    },

    /**
     * The header text for the sink list.
     * @type {string}
     */
    headerText: {
      type: String,
      value: '',
    },

    /**
     * The header text tooltip. This would be descriptive of the
     * source origin, whether a host name, tab URL, etc.
     * @type {string}
     */
    headerTextTooltip: {
      type: String,
      value: '',
    },

    /**
     * Whether the browser is currently incognito.
     * @type {boolean}
     */
    isOffTheRecord: {
      type: Boolean,
      value: false,
    },

    /**
     * Whether the search input is currently focused. This is used to prevent
     * window focus/blur events from interfering with input-focus-dependent
     * operations.
     * @private {boolean}
     */
    isSearchFocused_: {
      type: Boolean,
      value: false,
    },

    /**
     * Records the value of |isSearchFocused_| when a window blur event is
     * received. This used to handle search focus edge cases. See
     * |setSearchFocusHandlers_| for details.
     * @private {boolean}
     */
    isSearchFocusedOnWindowBlur_: {
      type: Boolean,
      value: false,
    },

    /**
     * Temporarily set when the window focus event handler needs to reset
     * |isSearchFocused_| to the correct value but needs to know whether the
     * search input focus handler will run. See |setSearchFocusHandlers_| for
     * details.
     * @private {boolean}
     */
    isSearchFocusedShouldBeSet_: {
      type: Boolean,
      value: false,
    },

    /**
     * Whether the user is currently searching for a sink.
     * @private {boolean}
     */
    isUserSearching_: {
      type: Boolean,
      value: false,
      observer: 'isUserSearchingChanged_',
    },

    /**
     * The issue to show.
     * @type {?media_router.Issue}
     */
    issue: {
      type: Object,
      value: null,
      observer: 'maybeShowIssueView_',
    },

    /**
     * The header text.
     * @private {string}
     */
    issueHeaderText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('issueHeader');
      },
    },

    /**
     * Whether the MR UI was just opened.
     * @private {boolean}
     */
    justOpened_: {
      type: Boolean,
      value: true,
    },

    /**
     * Whether the user's mouse is positioned over the dialog.
     * @private {boolean}
     */
    mouseIsPositionedOverDialog_: {
      type: Boolean,
      value: false,
    },

    /**
     * The ID of the route that is currently being created. This is set when
     * route creation is resolved but not ready for its controls to be
     * displayed.
     * @private {string}
     */
    pendingCreatedRouteId_: {
      type: String,
      value: '',
    },

    /**
     * The time the sink list was shown and populated with at least one sink.
     * This is reset whenever the user switches views or there are no sinks
     * available for display.
     * @private {number}
     */
    populatedSinkListSeenTimeMs_: {
      type: Number,
      value: -1,
    },

    /**
     * Whether the next character input should cause a filter action metric to
     * be sent.
     * @type {boolean}
     * @private
     */
    reportFilterOnInput_: {
      type: Boolean,
      value: false,
    },

    /**
     * The list of current routes.
     * @type {!Array<!media_router.Route>}
     */
    routeList: {
      type: Array,
      value: [],
      observer: 'rebuildRouteMaps_',
    },

    /**
     * Maps media_router.Route.id to corresponding media_router.Route.
     * @private {!Object<!string, !media_router.Route>}
     */
    routeMap_: {
      type: Object,
      value: {},
    },

    /**
     * Title text for the search button.
     * @private {string}
     */
    searchButtonTitle_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('searchButtonTitle');
      },
    },

    /**
     * Label text for the user search input.
     * @private {string}
     */
    searchInputLabel_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('searchInputLabel');
      },
    },

    /**
     * Search text entered by the user into the sink search input.
     * @private {string}
     */
    searchInputText_: {
      type: String,
      value: '',
      observer: 'searchInputTextChanged_',
    },

    /**
     * Text to display when a user search returns no matches.
     * @private {string}
     */
    searchNoMatchesText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('searchNoMatches');
      },
    },

    /**
     * Sinks to display that match |searchInputText_|.
     * @private {!Array<!media_router.Sink>}
     */
    searchResultsToShow_: {
      type: Array,
      value: [],
    },

    /**
     * The header text when the cast mode list is shown.
     * @private {string}
     */
    selectCastModeHeaderText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('selectCastModeHeader');
      },
    },

    /**
     * The subheading text for the non-cast-enabled app cast mode list.
     * @private {string}
     */
    shareYourScreenSubheadingText_: {
      type: String,
      readOnly: true,
      value: function() {
        return loadTimeData.getString('shareYourScreenSubheading');
      },
    },

    /**
     * Whether to show the user domain of sinks associated with identity.
     * @type {boolean}
     */
    showDomain: {
      type: Boolean,
      value: false,
    },

    /**
     * Whether to show the first run flow.
     * @type {boolean}
     */
    showFirstRunFlow: {
      type: Boolean,
      value: false,
      observer: 'updateElementPositioning_',
    },

    /**
     * Whether to show the cloud preference setting in the first run flow.
     * @type {boolean}
     */
    showFirstRunFlowCloudPref: {
      type: Boolean,
      value: false,
    },

    /**
     * The cast mode shown to the user. Initially set to auto mode. (See
     * media_router.CastMode documentation for details on auto mode.)
     * This value may be changed in one of the following ways:
     * 1) The user explicitly selected a cast mode.
     * 2) The user selected cast mode is no longer available for the associated
     *    WebContents. In this case, the container will reset to auto mode. Note
     *    that |userHasSelectedCastMode_| will switch back to false.
     * 3) The sink list changed, and the user had not explicitly selected a cast
     *    mode. If the sinks support exactly 1 cast mode, the container will
     *    switch to that cast mode. Otherwise, the container will reset to auto
     *    mode.
     * @private {number}
     */
    shownCastModeValue_: {
      type: Number,
      value: media_router.AUTO_CAST_MODE.type,
    },

    /**
     * Maps media_router.Sink.id to corresponding media_router.Sink.
     * @private {!Object<!string, !media_router.Sink>}
     */
    sinkMap_: {
      type: Object,
      value: {},
    },

    /**
     * Maps media_router.Sink.id to corresponding media_router.Route.
     * @private {!Object<!string, !media_router.Route>}
     */
    sinkToRouteMap_: {
      type: Object,
      value: {},
    },

    /**
     * Sinks to show for the currently selected cast mode.
     * @private {!Array<!media_router.Sink>}
     */
    sinksToShow_: {
      type: Array,
      value: [],
    },

    /**
     * Whether the user has explicitly selected a cast mode.
     * @private {boolean}
     */
    userHasSelectedCastMode_: {
      type: Boolean,
      value: false,
    },

    /**
     * Whether the user has already taken an action.
     * @type {boolean}
     */
    userHasTakenInitialAction_: {
      type: Boolean,
      value: false,
    },
  },

  listeners: {
    'focus': 'onFocus_',
    'header-height-changed': 'updateElementPositioning_',
    'header-or-arrow-click': 'toggleCastModeHidden_',
    'mouseleave': 'onMouseLeave_',
    'mouseenter': 'onMouseEnter_',
  },

  observers: [
    'maybeUpdateStartSinkDisplayStartTime_(currentView_, sinksToShow_)',
  ],

  ready: function() {
    this.elementReadyTimeMs_ = performance.now();

    // If this is not on a Mac platform, remove the placeholder. See
    // onFocus_() for more details. ready() is only called once, so no need
    // to check if the placeholder exist before removing.
    if (!cr.isMac)
      this.$$('#focus-placeholder').remove();

    document.addEventListener('keydown', this.onKeydown_.bind(this));
    this.setSearchFocusHandlers_();
    this.showSinkList_();
  },

  attached: function() {
    this.updateElementPositioning_();

    // Turn off the spinner after 3 seconds, then report the current number of
    // sinks.
    this.async(function() {
      this.justOpened_ = false;
      this.fire('report-sink-count', {
        sinkCount: this.allSinks.length,
      });
    }, 3000 /* 3 seconds */);
  },

  /**
   * Fires an acknowledge-first-run-flow event and hides the first run flow.
   * This is call when the first run flow button is clicked.
   *
   * @private
   */
  acknowledgeFirstRunFlow_: function() {
    // Only set |userOptedIntoCloudServices| if the user was shown the cloud
    // services preferences option.
    var userOptedIntoCloudServices = this.showFirstRunFlowCloudPref ?
        this.$$('#first-run-cloud-checkbox').checked : undefined;
    this.fire('acknowledge-first-run-flow', {
      optedIntoCloudServices: userOptedIntoCloudServices,
    });

    this.showFirstRunFlow = false;
    this.showFirstRunFlowCloudPref = false;
  },

  /**
   * Fires a 'report-initial-action' event when the user takes their first
   * action after the dialog opens. Also fires a 'report-initial-action-close'
   * event if that initial action is to close the dialog.
   * @param {!media_router.MediaRouterUserAction} initialAction
   */
  maybeReportUserFirstAction: function(initialAction) {
    if (this.userHasTakenInitialAction_)
      return;

    this.fire('report-initial-action', {
      action: initialAction,
    });

    if (initialAction == media_router.MediaRouterUserAction.CLOSE) {
      var timeToClose = performance.now() - this.elementReadyTimeMs_;
      this.fire('report-initial-action-close', {
        timeMs: timeToClose,
      });
    }

    this.userHasTakenInitialAction_ = true;
  },

  /**
   * Checks that the currently selected cast mode is still in the
   * updated list of available cast modes. If not, then update the selected
   * cast mode to the first available cast mode on the list.
   */
  checkCurrentCastMode_: function() {
    if (!this.castModeList.length)
      return;

    // If we are currently showing auto mode, then nothing needs to be done.
    // Otherwise, if the cast mode currently shown no longer exists (regardless
    // of whether it was selected by user), then switch back to auto cast mode.
    if (this.shownCastModeValue_ != media_router.CastModeType.AUTO &&
        !this.findCastModeByType_(this.shownCastModeValue_)) {
      this.setShownCastMode_(media_router.AUTO_CAST_MODE);
      this.rebuildSinksToShow_();
    }
  },

  /**
   * Compares two search match objects for sorting. Earlier and longer matches
   * are prioritized.
   *
   * @param {!{sinkItem: !media_router.Sink,
   *           substrings: Array<!Array<number>>}} resultA
   * Parameters in |resultA|:
   *   sinkItem - sink object.
   *   substrings - start-end index pairs of substring matches.
   * @param {!{sinkItem: !media_router.Sink,
   *           substrings: Array<!Array<number>>}} resultB
   * Parameters in |resultB|:
   *   sinkItem - sink object.
   *   substrings - start-end index pairs of substring matches.
   * @return {number} -1 if |resultA| should come before |resultB|, 1 if
   *     |resultB| should come before |resultA|, and 0 if they are considered
   *     equal.
   */
  compareSearchMatches_: function(resultA, resultB) {
    var substringsA = resultA.substrings;
    var substringsB = resultB.substrings;
    var numberSubstringsA = substringsA.length;
    var numberSubstringsB = substringsB.length;

    if (numberSubstringsA == 0 && numberSubstringsB == 0) {
      return 0;
    } else if (numberSubstringsA == 0) {
      return 1;
    } else if (numberSubstringsB == 0) {
      return -1;
    }

    var loopMax = Math.min(numberSubstringsA, numberSubstringsB);
    for (var i = 0; i < loopMax; ++i) {
      var [matchStartA, matchEndA] = substringsA[i];
      var [matchStartB, matchEndB] = substringsB[i];

      if (matchStartA < matchStartB) {
        return -1;
      } else if (matchStartA > matchStartB) {
        return 1;
      }

      if (matchEndA > matchEndB) {
        return -1;
      } else if (matchEndA < matchEndB) {
        return 1;
      }
    }

    if (numberSubstringsA > numberSubstringsB) {
      return -1;
    } else if (numberSubstringsA < numberSubstringsB) {
      return 1;
    }
    return 0;
  },

  /**
   * If |allSinks| supports only a single cast mode, returns that cast mode.
   * Otherwise, returns AUTO_MODE. Only called if |userHasSelectedCastMode_| is
   * |false|.
   * @return {!media_router.CastMode} The single cast mode supported by
   *                                  |allSinks|, or AUTO_MODE.
   */
  computeCastMode_: function() {
    var allCastModes = this.allSinks.reduce(function(castModesSoFar, sink) {
      return castModesSoFar | sink.castModes;
    }, 0);

    // This checks whether |castModes| does not consist of exactly 1 cast mode.
    if (!allCastModes || allCastModes & (allCastModes - 1))
      return media_router.AUTO_CAST_MODE;

    var castMode = this.findCastModeByType_(allCastModes);
    if (castMode)
      return castMode;

    console.error('Cast mode ' + allCastModes + ' not in castModeList');
    return media_router.AUTO_CAST_MODE;
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @return {boolean} Whether or not to hide the cast mode list.
   * @private
   */
  computeCastModeListHidden_: function(view) {
    return view != media_router.MediaRouterView.CAST_MODE_LIST;
  },

  /**
   * @param {!media_router.CastMode} castMode The cast mode to determine an
   *     icon for.
   * @return {string} The Polymer <iron-icon> icon to use. The format is
   *     <iconset>:<icon>, where <iconset> is the set ID and <icon> is the name
   *     of the icon. <iconset>: may be omitted if <icon> is from the default
   *     set.
   * @private
   */
  computeCastModeIcon_: function(castMode) {
    switch (castMode.type) {
      case media_router.CastModeType.DEFAULT:
        return 'av:web';
      case media_router.CastModeType.TAB_MIRROR:
        return 'tab';
      case media_router.CastModeType.DESKTOP_MIRROR:
        return 'hardware:laptop';
      default:
        return '';
    }
  },

  /**
   * @param {!Array<!media_router.CastMode>} castModeList The current list of
   *      cast modes.
   * @return {!Array<!media_router.CastMode>} The list of default cast modes.
   * @private
   */
  computeDefaultCastModeList_: function(castModeList) {
    return castModeList.filter(function(mode) {
      return mode.type == media_router.CastModeType.DEFAULT;
    });
  },

  /**
   * @param {!Array<!media_router.Sink>} sinksToShow The list of sinks.
   * @return {boolean} Whether or not to hide the 'devices missing' message.
   * @private
   */
  computeDeviceMissingHidden_: function(sinksToShow) {
    return sinksToShow.length != 0;
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @param {?media_router.Issue} issue The current issue.
   * @return {boolean} Whether or not to hide the header.
   * @private
   */
  computeHeaderHidden_: function(view, issue) {
    return view == media_router.MediaRouterView.ROUTE_DETAILS ||
        (view == media_router.MediaRouterView.SINK_LIST &&
         !!issue && issue.isBlocking);
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @param {string} headerText The header text for the sink list.
   * @return {string} The text for the header.
   * @private
   */
  computeHeaderText_: function(view, headerText) {
    switch (view) {
      case media_router.MediaRouterView.CAST_MODE_LIST:
        return this.selectCastModeHeaderText_;
      case media_router.MediaRouterView.ISSUE:
        return this.issueHeaderText_;
      case media_router.MediaRouterView.ROUTE_DETAILS:
        return this.currentRoute_ ?
            this.sinkMap_[this.currentRoute_.sinkId].name : '';
      case media_router.MediaRouterView.SINK_LIST:
      case media_router.MediaRouterView.FILTER:
        return this.headerText;
      default:
        return '';
    }
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @param {string} headerTooltip The tooltip for the header for the sink
   *     list.
   * @return {string} The tooltip for the header.
   * @private
   */
  computeHeaderTooltip_: function(view, headerTooltip) {
    return view == media_router.MediaRouterView.SINK_LIST ? headerTooltip : '';
  },

  /**
   * @param {string} currentLaunchingSinkId ID of the sink that is currently
   *     launching, or empty string if none exists.
   * @private
   */
  computeIsLaunching_: function(currentLaunchingSinkId) {
    return currentLaunchingSinkId != '';
  },

  /**
   * @param {?media_router.Issue} issue The current issue.
   * @return {string} The class for the issue banner.
   * @private
   */
  computeIssueBannerClass_: function(issue) {
    return !!issue && !issue.isBlocking ? 'non-blocking' : '';
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @param {?media_router.Issue} issue The current issue.
   * @return {boolean} Whether or not to show the issue banner.
   * @private
   */
  computeIssueBannerShown_: function(view, issue) {
    return !!issue && (view == media_router.MediaRouterView.SINK_LIST ||
                       view == media_router.MediaRouterView.ISSUE);
  },

  /**
   * @param {!Array<!media_router.Sink>} searchResultsToShow The sinks currently
   *     matching the search text.
   * @param {boolean} isUserSearching Whether the user is searching for sinks.
   * @return {boolean} Whether or not the 'no matches' message is hidden.
   * @private
   */
  computeNoMatchesHidden_: function(searchResultsToShow, isUserSearching) {
    return !isUserSearching || this.searchInputText_.length == 0 ||
           searchResultsToShow.length != 0;
  },

  /**
   * @param {!Array<!media_router.CastMode>} castModeList The current list of
   *     cast modes.
   * @return {!Array<!media_router.CastMode>} The list of non-default cast
   *     modes.
   * @private
   */
  computeNonDefaultCastModeList_: function(castModeList) {
    return castModeList.filter(function(mode) {
      return mode.type != media_router.CastModeType.DEFAULT;
    });
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @param {?media_router.Issue} issue The current issue.
   * @return {boolean} Whether or not to hide the route details.
   * @private
   */
  computeRouteDetailsHidden_: function(view, issue) {
    return view != media_router.MediaRouterView.ROUTE_DETAILS ||
        (!!issue && issue.isBlocking);
  },

  /**
   * Computes an array of substring indices that mark where substrings of
   * |searchString| occur in |sinkName|.
   *
   * @param {string} searchString Search string entered by user.
   * @param {string} sinkName Sink name being filtered.
   * @return {Array<!Array<number>>} Array of substring start-end (inclusive)
   *     index pairs if every character in |searchString| was matched, in order,
   *     in |sinkName|. Otherwise it returns null.
   * @private
   */
  computeSearchMatches_: function(searchString, sinkName) {
    var i = 0;
    var matchStart = -1;
    var matchEnd = -1;
    var matchPairs = [];
    for (var j = 0; i < searchString.length && j < sinkName.length; ++j) {
      if (searchString[i].toLocaleLowerCase() ==
          sinkName[j].toLocaleLowerCase()) {
        if (matchStart == -1) {
          matchStart = j;
        }
        ++i;
      } else if (matchStart != -1) {
        matchEnd = j - 1;
        matchPairs.push([matchStart, matchEnd]);
        matchStart = -1;
      }
    }
    if (matchStart != -1) {
      matchEnd = j - 1;
      matchPairs.push([matchStart, matchEnd]);
    }
    return (i == searchString.length) ? matchPairs : null;
  },

  /**
   * Computes whether the search results list should be hidden.
   * @param {boolean} isUserSearching Whether the user is searching for sinks.
   * @param {!Array<!media_router.Sink>} searchResultsToShow The sinks currently
   * @return {boolean} Whether the search results list should be hidden.
   * @private
   */
  computeSearchResultsHidden_: function(isUserSearching, searchResultsToShow) {
    return !isUserSearching || searchResultsToShow.length == 0;
  },

  /**
   * @param {!Array<!media_router.CastMode>} castModeList The current list of
   *     cast modes.
   * @return {boolean} Whether or not to hide the share screen subheading text.
   * @private
   */
  computeShareScreenSubheadingHidden_: function(castModeList) {
    return this.computeNonDefaultCastModeList_(castModeList).length == 0;
  },

  /**
   * @param {boolean} showFirstRunFlow Whether or not to show the first run
   *     flow.
   * @param {?media_router.MediaRouterView} currentView The current view.
   * @private
   */
  computeShowFirstRunFlow_: function(showFirstRunFlow, currentView) {
    return showFirstRunFlow &&
        currentView == media_router.MediaRouterView.SINK_LIST;
  },

  /**
   * @param {!media_router.Sink} sink The sink to determine an icon for.
   * @return {string} The Polymer <iron-icon> icon to use. The format is
   *     <iconset>:<icon>, where <iconset> is the set ID and <icon> is the name
   *     of the icon. <iconset>: may be ommitted if <icon> is from the default
   *     set.
   * @private
   */
  computeSinkIcon_: function(sink) {
    switch (sink.iconType) {
      case media_router.SinkIconType.CAST:
        return 'media-router:chromecast';
      case media_router.SinkIconType.CAST_AUDIO:
        return 'hardware:speaker';
      case media_router.SinkIconType.CAST_AUDIO_GROUP:
        return 'hardware:speaker-group';
      case media_router.SinkIconType.GENERIC:
        return 'hardware:tv';
      case media_router.SinkIconType.HANGOUT:
        return 'media-router:hangout';
      default:
        return 'hardware:tv';
    }
  },

  /**
   * @param {!string} sinkId A sink ID.
   * @param {!Object<!string, ?media_router.Route>} sinkToRouteMap
   *     Maps media_router.Sink.id to corresponding media_router.Route.
   * @return {string} The class for the sink icon.
   * @private
   */
  computeSinkIconClass_: function(sinkId, sinkToRouteMap) {
    return sinkToRouteMap[sinkId] ? 'sink-icon active-sink' : 'sink-icon';
  },

  /**
   * @param {!string} currentLaunchingSinkId The ID of the sink that is
   *     currently launching.
   * @param {!string} sinkId A sink ID.
   * @return {boolean} |true| if given sink is currently launching.
   * @private
   */
  computeSinkIsLaunching_: function(currentLaunchingSinkId, sinkId) {
    return currentLaunchingSinkId == sinkId;
  },

  /**
   * @param {!Array<!media_router.Sink>} sinksToShow The list of sinks.
   * @param {boolean} isUserSearching Whether the user is searching for sinks.
   * @return {boolean} Whether or not to hide the sink list.
   * @private
   */
  computeSinkListHidden_: function(sinksToShow, isUserSearching) {
    return sinksToShow.length == 0 || isUserSearching;
  },

  /**
   * @param {?media_router.MediaRouterView} view The current view.
   * @param {?media_router.Issue} issue The current issue.
   * @return {boolean} Whether or not to hide entire the sink list view.
   * @private
   */
  computeSinkListViewHidden_: function(view, issue) {
    return (view != media_router.MediaRouterView.SINK_LIST &&
            view != media_router.MediaRouterView.FILTER) ||
        (!!issue && issue.isBlocking);
  },

  /**
   * Returns whether the sink domain for |sink| should be hidden.
   * @param {!media_router.Sink} sink
   * @return {boolean} |true| if the domain should be hidden.
   * @private
   */
  computeSinkDomainHidden_: function(sink) {
    return !this.showDomain || this.isEmptyOrWhitespace_(sink.domain);
  },

  /**
   * Computes which portions of a sink name, if any, should be highlighted when
   * displayed in the filter view. Any substrings matching the search text
   * should be highlighted.
   *
   * The order the strings are combined is plainText[0] highlightedText[0]
   * plainText[1] highlightedText[1] etc.
   *
   * @param {!{sinkItem: !media_router.Sink,
   *           substrings: !Array<!Array<number>>}} matchedItem
   * Parameters in matchedItem:
   *   sinkItem - Original !media_router.Sink from the sink list.
   *   substrings - List of index pairs denoting substrings of sinkItem.name
   *       that match |searchInputText_|.
   * @return {!{highlightedText: !Array<string>, plainText: !Array<string>}}
   *   highlightedText - Array of strings that should be displayed highlighted.
   *   plainText - Array of strings that should be displayed normally.
   * @private
   */
  computeSinkMatchingText_: function(matchedItem) {
    if (!matchedItem.substrings) {
      return {highlightedText: [null], plainText: [matchedItem.sinkItem.name]};
    }
    var lastMatchIndex = -1;
    var nameIndex = 0;
    var sinkName = matchedItem.sinkItem.name;
    var highlightedText = [];
    var plainText = [];
    for (var i = 0; i < matchedItem.substrings.length; ++i) {
      var [matchStart, matchEnd] = matchedItem.substrings[i];
      if (lastMatchIndex + 1 < matchStart) {
        plainText.push(sinkName.substring(lastMatchIndex + 1, matchStart));
      } else {
        plainText.push(null);
      }
      highlightedText.push(sinkName.substring(matchStart, matchEnd + 1));
      lastMatchIndex = matchEnd;
    }
    if (lastMatchIndex + 1 < sinkName.length) {
      highlightedText.push(null);
      plainText.push(sinkName.substring(lastMatchIndex + 1));
    }
    return {highlightedText: highlightedText, plainText: plainText};
  },

  /**
   * Computes the CSS class for #sink-search depending on whether it is the
   * first or last item in the list, as indicated by |currentView|.
   * @param {?media_router.MediaRouterView} currentView The current view of the
   *     dialog.
   * @return {string} The CSS that correctly sets the padding of #sink-search
   *     for the current view.
   * @private
   */
  computeSinkSearchClass_: function(currentView) {
    return (currentView == media_router.MediaRouterView.FILTER) ? '' : 'bottom';
  },

  /**
   * Returns the subtext to be shown for |sink|. Only called if
   * |computeSinkSubtextHidden_| returns false for the same |sink| and
   * |sinkToRouteMap|.
   * @param {!media_router.Sink} sink
   * @param {!Object<!string, ?media_router.Route>} sinkToRouteMap
   * @return {?string} The subtext to be shown.
   * @private
   */
  computeSinkSubtext_: function(sink, sinkToRouteMap) {
    var route = sinkToRouteMap[sink.id];
    if (route && !this.isEmptyOrWhitespace_(route.description))
      return route.description;

    return sink.description;
  },

  /**
   * Returns whether the sink subtext for |sink| should be hidden.
   * @param {!media_router.Sink} sink
   * @param {!Object<!string, ?media_router.Route>} sinkToRouteMap
   * @return {boolean} |true| if the subtext should be hidden.
   * @private
   */
  computeSinkSubtextHidden_: function(sink, sinkToRouteMap) {
    if (!this.isEmptyOrWhitespace_(sink.description))
      return false;

    var route = sinkToRouteMap[sink.id];
    return !route || this.isEmptyOrWhitespace_(route.description);
  },

  /**
   * @param {boolean} justOpened Whether the MR UI was just opened.
   * @return {boolean} Whether or not to hide the spinner.
   * @private
   */
  computeSpinnerHidden_: function(justOpened) {
    return !justOpened;
  },

  /**
   * Updates element positioning when the view changes and possibly triggers
   * reporting of a user filter action. If there is no filter text, it defers
   * the reporting until some text is entered, but otherwise it reports the
   * filter action here.
   * @param {?media_router.MediaRouterView} currentView The current view of the
   *     dialog.
   * @private
   */
  currentViewChanged_: function(currentView) {
    if (currentView == media_router.MediaRouterView.FILTER) {
      this.reportFilterOnInput_ = true;
      this.maybeReportFilter_();
    }
    this.updateElementPositioning_();
  },

  /**
   * Filters all sinks based on fuzzy matching to the currently entered search
   * text.
   * @param {string} searchInputText The currently entered search text.
   * @private
   */
  filterSinks_: function(searchInputText) {
    if (searchInputText.length == 0) {
      this.searchResultsToShow_ = this.sinksToShow_.map(function(item) {
        return {sinkItem: item, substrings: null};
      });
      return;
    }
    this.isUserSearching_ = true;

    var searchResultsToShow = [];
    for (var i = 0; i < this.sinksToShow_.length; ++i) {
      var matchSubstrings = this.computeSearchMatches_(
          searchInputText,
          this.sinksToShow_[i].name);
      if (!matchSubstrings) {
        continue;
      }
      searchResultsToShow.push({sinkItem: this.sinksToShow_[i],
                                substrings: matchSubstrings});
    }
    searchResultsToShow.sort(this.compareSearchMatches_);
    this.searchResultsToShow_ = searchResultsToShow;
  },

  /**
   * Helper function to locate the CastMode object with the given type in
   * castModeList.
   *
   * @param {number} castModeType Type of cast mode to look for.
   * @return {media_router.CastMode|undefined} CastMode object with the given
   *     type in castModeList, or undefined if not found.
   */
  findCastModeByType_: function(castModeType) {
    return this.castModeList.find(function(element, index, array) {
      return element.type == castModeType;
    });
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
   * Updates sink list when user is searching.
   * @param {boolean} isUserSearching Whether the user is searching for sinks.
   */
  isUserSearchingChanged_: function(isUserSearching) {
    if (isUserSearching) {
      this.currentView_ = media_router.MediaRouterView.FILTER;
      this.updateElementPositioning_();
      this.filterSinks_(this.searchInputText_);
    } else {
      this.currentView_ = media_router.MediaRouterView.SINK_LIST;
    }
  },

  /**
   * Reports a user filter action if |searchInputText_| is not empty and the
   * filter action hasn't been reported since the view changed to the filter
   * view.
   * @private
   */
  maybeReportFilter_: function() {
    if (this.reportFilterOnInput_ && this.searchInputText_.length != 0) {
      this.reportFilterOnInput_ = false;
      this.fire('report-filter');
    }
  },

  /**
   * Updates |currentView_| if the dialog had just opened and there's
   * only one local route.
   */
  maybeShowRouteDetailsOnOpen: function() {
    var localRoute = null;
    for (var i = 0; i < this.routeList.length; i++) {
      var route = this.routeList[i];
      if (!route.isLocal)
        continue;
      if (!localRoute) {
        localRoute = route;
      } else {
        // Don't show route details if there are more than one local route.
        localRoute = null;
        break;
      }
    }

    if (localRoute)
      this.showRouteDetails_(localRoute);
    this.fire('show-initial-state', {currentView: this.currentView_});
  },

  /**
   * Updates |currentView_| if there is a new blocking issue or a blocking
   * issue is resolved. Clears any pending route creation properties if the
   * issue corresponds with |pendingCreatedRouteId_|.
   *
   * @param {?media_router.Issue} issue The new issue, or null if the
   *                              blocking issue was resolved.
   * @private
   */
  maybeShowIssueView_: function(issue) {
    if (!!issue) {
      if (issue.isBlocking) {
        this.currentView_ = media_router.MediaRouterView.ISSUE;
      } else if (this.currentView_ == media_router.MediaRouterView.SINK_LIST) {
        // Make space for the non-blocking issue in the sink list.
        this.updateElementPositioning_();
      }
    } else {
      // Switch back to the sink list if the issue was cleared. If the previous
      // issue was non-blocking, this would be a no-op. It is expected that
      // the only way to clear an issue is by user action; the IssueManager
      // (C++ side) does not clear issues in the UI.
      this.currentView_ = media_router.MediaRouterView.SINK_LIST;
    }

    if (!!this.pendingCreatedRouteId_ && !!issue &&
        issue.routeId == this.pendingCreatedRouteId_) {
      this.resetRouteCreationProperties_(false);
    }
  },

  /**
   * May update |populatedSinkListSeenTimeMs_| depending on |currentView| and
   * |sinksToShow|.
   * Called when |currentView_| or |sinksToShow_| is updated.
   *
   * @param {?media_router.MediaRouterView} currentView The current view of the
   *                                        dialog.
   * @param {!Array<!media_router.Sink>} sinksToShow The sinks to display.
   * @private
   */
  maybeUpdateStartSinkDisplayStartTime_: function(currentView, sinksToShow) {
    if (currentView == media_router.MediaRouterView.SINK_LIST &&
        sinksToShow.length != 0) {
      // Only set |populatedSinkListSeenTimeMs_| if it has not already been set.
      if (this.populatedSinkListSeenTimeMs_ == -1)
        this.populatedSinkListSeenTimeMs_ = performance.now();
    } else {
      // Reset |populatedSinkListLastSeen_| if the sink list isn't being shown
      // or if there aren't any sinks available for display.
      this.populatedSinkListSeenTimeMs_ = -1;
    }
  },

  /**
   * Handles a cast mode selection. Updates |headerText|, |headerTextTooltip|,
   * and |shownCastModeValue_|.
   *
   * @param {!Event} event The event object.
   * @private
   */
  onCastModeClick_: function(event) {
    // The clicked cast mode can come from one of two lists,
    // defaultCastModeList and nonDefaultCastModeList.
    var clickedMode =
        this.$$('#defaultCastModeList').itemForElement(event.target) ||
            this.$$('#nonDefaultCastModeList').itemForElement(event.target);

    if (!clickedMode)
      return;

    this.userHasSelectedCastMode_ = true;
    this.fire('cast-mode-selected', {castModeType: clickedMode.type});

    // The list of sinks to show will be the same if the shown cast mode did
    // not change, regardless of whether the user selected it explicitly.
    if (clickedMode.type != this.shownCastModeValue_) {
      this.setShownCastMode_(clickedMode);
      this.rebuildSinksToShow_();
    }

    this.showSinkList_();
    this.maybeReportUserFirstAction(
        media_router.MediaRouterUserAction.CHANGE_MODE);
  },

  /**
   * Handles a close-route-click event. Shows the sink list and starts a timer
   * to close the dialog if there is no click within three seconds.
   *
   * @param {!Event} event The event object.
   * Parameters in |event|.detail:
   *   route - route to close.
   * @private
   */
  onCloseRouteClick_: function(event) {
    /** @type {{route: media_router.Route}} */
    var detail = event.detail;
    this.showSinkList_();
    this.startTapTimer_();

    if (detail.route.isLocal) {
      this.maybeReportUserFirstAction(
          media_router.MediaRouterUserAction.STOP_LOCAL);
    }
  },

  /**
   * Handles response of previous create route attempt.
   *
   * @param {string} sinkId The ID of the sink to which the Media Route was
   *     creating a route.
   * @param {?media_router.Route} route The newly created route that
   *     corresponds to the sink if route creation succeeded; null otherwise.
   * @param {boolean} isForDisplay Whether or not |route| is for display.
   */
  onCreateRouteResponseReceived: function(sinkId, route, isForDisplay) {
    // The provider will handle sending an issue for a failed route request.
    if (!route) {
      this.resetRouteCreationProperties_(false);
      this.fire('report-resolved-route', {
        outcome: media_router.MediaRouterRouteCreationOutcome.FAILURE_NO_ROUTE
      });
      return;
    }

    // Check that |sinkId| exists and corresponds to |currentLaunchingSinkId_|.
    if (!this.sinkMap_[sinkId] || this.currentLaunchingSinkId_ != sinkId) {
      this.fire('report-resolved-route', {
        outcome:
            media_router.MediaRouterRouteCreationOutcome.FAILURE_INVALID_SINK
      });
      return;
    }

    // Regardless of whether the route is for display, it was resolved
    // successfully.
    this.fire('report-resolved-route', {
      outcome: media_router.MediaRouterRouteCreationOutcome.SUCCESS
    });

    if (isForDisplay) {
      this.showRouteDetails_(route);
      this.startTapTimer_();
      this.resetRouteCreationProperties_(true);
    } else {
      this.pendingCreatedRouteId_ = route.id;
    }
  },

  /**
   * Called when a focus event is triggered.
   *
   * @param {!Event} event The event object.
   * @private
   */
  onFocus_: function(event) {
    // If the focus event was automatically fired by Polymer, remove focus from
    // the element. This prevents unexpected focusing when the dialog is
    // initially loaded. This only happens on mac.
    if (cr.isMac && !event.sourceCapabilities) {
      // Adding a focus placeholder element is part of the workaround for
      // handling unexpected focusing, which only happens once on dialog open.
      // Since the placeholder is focus-enabled as denoted by its tabindex
      // value, the focus will not appear in other elements.
      var placeholder = this.$$('#focus-placeholder');
      // Check that the placeholder is the currently focused element. In some
      // tests, other elements are non-user-triggered focused.
      if (placeholder && this.shadowRoot.activeElement == placeholder) {
        event.path[0].blur();
        // Remove the placeholder since we have no more use for it.
        placeholder.remove();
      }
    }
  },

  /**
   * Called when a keydown event is fired.
   * @param {!Event} e Keydown event object for the event.
   */
  onKeydown_: function(e) {
    // The ESC key may be pressed with a combination of other keys. It is
    // handled on the C++ side instead of the JS side on non-mac platforms,
    // which uses toolkit-views. Handle the expected behavior on all platforms
    // here.
    if (e.keyCode == media_router.KEYCODE_ESC && !e.shiftKey &&
        !e.ctrlKey && !e.altKey && !e.metaKey) {
      // When searching, allow ESC as a mechanism to leave the filter view.
      if (this.isUserSearching_) {
        this.showSinkList_();
        e.preventDefault();
      } else {
        this.fire('close-dialog', {
          pressEscToClose: true,
        });
      }
    }
  },

  /**
   * Called when a mouseleave event is triggered.
   *
   * @private
   */
  onMouseLeave_: function() {
    this.mouseIsPositionedOverDialog_ = false;
  },

  /**
   * Called when a mouseenter event is triggered.
   *
   * @private
   */
  onMouseEnter_: function() {
    this.mouseIsPositionedOverDialog_ = true;
  },

  /**
   * Called when a sink is clicked.
   *
   * @param {!Event} event The event object.
   * @private
   */
  onSinkClick_: function(event) {
    var clickedSink = (this.isUserSearching_) ?
        this.$$('#searchResults').itemForElement(event.target).sinkItem :
        this.$.sinkList.itemForElement(event.target);
    this.showOrCreateRoute_(clickedSink);
    this.fire('sink-click', {index: event['model'].index});
  },

  /**
   * Called when |routeList| is updated. Rebuilds |routeMap_| and
   * |sinkToRouteMap_|.
   *
   * @private
   */
  rebuildRouteMaps_: function() {
    this.routeMap_ = {};

    // Rebuild |sinkToRouteMap_| with a temporary map to avoid firing the
    // computed functions prematurely.
    var tempSinkToRouteMap = {};

    // We expect that each route in |routeList| maps to a unique sink.
    this.routeList.forEach(function(route) {
      this.routeMap_[route.id] = route;
      tempSinkToRouteMap[route.sinkId] = route;
    }, this);

    // If there is route creation in progress, check if any of the route ids
    // correspond to |pendingCreatedRouteId_|. If so, the newly created route
    // is ready to be displayed; switch to route details view.
    if (this.currentLaunchingSinkId_ != '' &&
        this.pendingCreatedRouteId_ != '') {
      var route = tempSinkToRouteMap[this.currentLaunchingSinkId_];
      if (route && this.pendingCreatedRouteId_ == route.id) {
        this.showRouteDetails_(route);
        this.startTapTimer_();
        this.resetRouteCreationProperties_(true);
      }
    } else {
      // If |currentRoute_| is no longer active, clear |currentRoute_|. Also
      // switch back to the SINK_PICKER view if the user is currently in the
      // ROUTE_DETAILS view.
      if (this.currentRoute_) {
        this.currentRoute_ = this.routeMap_[this.currentRoute_.id] || null;
      }
      if (!this.currentRoute_ &&
          this.currentView_ == media_router.MediaRouterView.ROUTE_DETAILS) {
        this.showSinkList_();
      }
    }

    this.sinkToRouteMap_ = tempSinkToRouteMap;
    this.rebuildSinksToShow_();
  },

  /**
   * Rebuilds the list of sinks to be shown for the current cast mode.
   * A sink should be shown if it is compatible with the current cast mode, or
   * if the sink is associated with a route.  The resulting list is sorted by
   * name.
   */
  rebuildSinksToShow_: function() {
    var sinksToShow = [];
    if (this.userHasSelectedCastMode_) {
      // If user explicitly selected a cast mode, then we show only sinks that
      // are compatible with current cast mode or sinks that are active.
      sinksToShow = this.allSinks.filter(function(element) {
        return (element.castModes & this.shownCastModeValue_) ||
               this.sinkToRouteMap_[element.id];
      }, this);
    } else {
      // If user did not select a cast mode, then:
      // - If all sinks support only a single cast mode, then the cast mode is
      //   switched to that mode.
      // - Otherwise, the cast mode becomes auto mode.
      // Either way, all sinks will be shown.
      this.setShownCastMode_(this.computeCastMode_());
      sinksToShow = this.allSinks;
    }

    this.sinksToShow_ = sinksToShow;
  },

  /**
   * Called when |allSinks| is updated.
   *
   * @private
   */
  reindexSinksAndRebuildSinksToShow_: function() {
    this.sinkMap_ = {};

    this.allSinks.forEach(function(sink) {
      this.sinkMap_[sink.id] = sink;
    }, this);

    this.rebuildSinksToShow_();
    if (this.isUserSearching_) {
      this.filterSinks_(this.searchInputText_);
    }
  },

  /**
   * Resets the properties relevant to creating a new route. Fires an event
   * indicating whether or not route creation was successful.
   * Clearing |currentLaunchingSinkId_| hides the spinner indicating there is
   * a route creation in progress and show the device icon instead.
   *
   * @private
   */
  resetRouteCreationProperties_: function(creationSuccess) {
    this.currentLaunchingSinkId_ = '';
    this.pendingCreatedRouteId_ = '';

    this.fire('report-route-creation', {success: creationSuccess});
  },

  /**
   * Responds to a click on the search button by toggling sink filtering.
   */
  searchButtonClick_: function() {
    // Redundancy needed because focus() only fires event if input is not
    // already focused. In the case that user typed text, hit escape, then
    // clicks the search button, a focus event will not fire and so its event
    // handler from ready() will not run.
    this.isUserSearching_ = true;
    this.$$('#sink-search-input').focus();
  },

  /**
   * Sets various focus and blur event handlers to handle |isSearchFocused_| and
   * showing search results when the input is focused.
   * @private
   */
  setSearchFocusHandlers_: function() {
    var search = this.$['sink-search-input'];

    // The window can see a blur event for two important cases: the window is
    // actually losing focus or keyboard focus is wrapping from the end of the
    // document to the beginning. To handle both cases, we save the state of
    // |isSearchFocused_| during the window blur event.
    //
    // The corresponding window focus event can do nothing if |isSearchFocused_|
    // was false during the blur event. If the search input is gaining focus now
    // it will work correctly. There are two cases when the input had focus
    // during the window blur event: the input still has focus and the input
    // lost focus. These cases are handled by the logic around
    // |isSearchFocusedShouldBeSet_| and |isSearchFocused_|.
    //
    // Because the window focus event will always happen first, it doesn't know
    // whether the input focus handler will later run or not. If it is not going
    // to run, then |isSearchFocused_| should be set to |false|, otherwise it
    // should be |true|. So the window focus handler just sets
    // |isSearchFocused_| to |false| and makes a note in
    // |isSearchFocusedShouldBeSet_| that |isSearchFocused_| should actually be
    // set to true if the search focus handler runs. The |setTimeout| in the
    // window focus handler clears this note as soon as all focus event handlers
    // have run.
    window.addEventListener('blur', function() {
      this.isSearchFocusedOnWindowBlur_ = this.isSearchFocused_;
    }.bind(this));
    window.addEventListener('focus', function() {
      if (this.isSearchFocusedOnWindowBlur_) {
        this.isSearchFocusedOnWindowBlur_ = false;
        this.isSearchFocusedShouldBeSet_ = true;
        this.isSearchFocused_ = false;
        setTimeout(function() {
          this.isSearchFocusedShouldBeSet_ = false;
        }.bind(this));
      }
    }.bind(this));
    search.addEventListener('blur', function() {
      // This lets normal blur cases work as expected, but doesn't get in the
      // way of the window blur handler capturing the "current" state. This is
      // the case because this will be run after all the blur handlers are done.
      setTimeout(function() { this.isSearchFocused_ = false; }.bind(this));
    }.bind(this));
    search.addEventListener('focus', function() {
      if (this.isSearchFocusedShouldBeSet_) {
        this.isSearchFocused_ = true;
      }
      if (!this.isSearchFocused_) {
        this.isSearchFocused_ = true;
        this.isUserSearching_ = true;
      }
    }.bind(this));
  },

  /**
   * Filters the  sink list when the input text changes and shows the search
   * results if |searchInputText| is not empty.
   * @param {string} searchInputText The currently entered search text.
   * @private
   */
  searchInputTextChanged_: function(searchInputText) {
    this.filterSinks_(searchInputText);
    if (searchInputText.length != 0) {
      this.isUserSearching_ = true;
      this.maybeReportFilter_();
    }
  },

  /**
   * Updates the shown cast mode, and updates the header text fields
   * according to the cast mode. If |castMode| type is AUTO, then set
   * |userHasSelectedCastMode_| to false.
   *
   * @param {!media_router.CastMode} castMode
   */
  setShownCastMode_: function(castMode) {
    if (this.shownCastModeValue_ == castMode.type)
      return;

    this.shownCastModeValue_ = castMode.type;
    this.headerText = castMode.description;
    this.headerTextTooltip = castMode.host || '';
    if (castMode.type == media_router.CastModeType.AUTO)
      this.userHasSelectedCastMode_ = false;
  },

  /**
   * Shows the cast mode list.
   *
   * @private
   */
  showCastModeList_: function() {
    this.currentView_ = media_router.MediaRouterView.CAST_MODE_LIST;
  },

  /**
   * Creates a new route if there is no route to the |sink| . Otherwise,
   * shows the route details.
   *
   * @param {!media_router.Sink} sink The sink to use.
   * @private
   */
  showOrCreateRoute_: function(sink) {
    var route = this.sinkToRouteMap_[sink.id];
    if (route) {
      this.showRouteDetails_(route);
      this.fire('navigate-sink-list-to-details');
      this.maybeReportUserFirstAction(
          media_router.MediaRouterUserAction.STATUS_REMOTE);
    } else if (this.currentLaunchingSinkId_ == '') {
      // Allow one launch at a time.
      this.fire('create-route', {
        sinkId: sink.id,
        // If user selected a cast mode, then we will create a route using that
        // cast mode. Otherwise, the UI is in "auto" cast mode and will use the
        // preferred cast mode compatible with the sink. The preferred cast mode
        // value is the least significant bit on the bitset.
        selectedCastModeValue:
            this.shownCastModeValue_ == media_router.CastModeType.AUTO ?
                sink.castModes & -sink.castModes : this.shownCastModeValue_
      });
      this.currentLaunchingSinkId_ = sink.id;

      var timeToSelectSink =
          performance.now() - this.populatedSinkListSeenTimeMs_;
      this.fire('report-sink-click-time', {timeMs: timeToSelectSink});

      this.maybeReportUserFirstAction(
          media_router.MediaRouterUserAction.START_LOCAL);
    }
  },

  /**
   * Shows the route details.
   *
   * @param {!media_router.Route} route The route to show.
   * @private
   */
  showRouteDetails_: function(route) {
    this.currentRoute_ = route;
    this.currentView_ = media_router.MediaRouterView.ROUTE_DETAILS;
  },

  /**
   * Shows the sink list.
   *
   * @private
   */
  showSinkList_: function() {
    this.currentView_ = media_router.MediaRouterView.SINK_LIST;
    this.isUserSearching_ = false;
  },

  /**
   * Starts a timer which fires a close-dialog event if the user's mouse is
   * not positioned over the dialog after three seconds.
   *
   * @private
   */
  startTapTimer_: function() {
    var id = setTimeout(function() {
      if (!this.mouseIsPositionedOverDialog_)
        this.fire('close-dialog', {
          pressEscToClose: false,
        });
    }.bind(this), 3000 /* 3 seconds */);
  },

  /**
   * Toggles |currentView_| between CAST_MODE_LIST and SINK_LIST.
   *
   * @private
   */
  toggleCastModeHidden_: function() {
    if (this.currentView_ == media_router.MediaRouterView.CAST_MODE_LIST) {
      this.showSinkList_();
    } else if (this.currentView_ == media_router.MediaRouterView.SINK_LIST) {
      this.showCastModeList_();
      this.fire('navigate-to-cast-mode-list');
    }
  },

  /**
   * Update the position-related styling of some elements.
   *
   * @private
   */
  updateElementPositioning_: function() {
    // Ensures that conditionally templated elements have finished stamping.
    this.async(function() {
      var headerHeight = this.$$('#container-header').offsetHeight;
      var firstRunFlowHeight = this.$$('#first-run-flow') &&
          this.$$('#first-run-flow').style.display != 'none' ?
              this.$$('#first-run-flow').offsetHeight : 0;
      var issueHeight = this.$$('#issue-banner') &&
          this.$$('#issue-banner').style.display != 'none' ?
              this.$$('#issue-banner').offsetHeight : 0;
      var searchHeight = this.$$('#sink-search').offsetHeight;

      this.$['container-header'].style.marginTop = firstRunFlowHeight + 'px';
      this.$['content'].style.marginTop =
          firstRunFlowHeight + headerHeight + 'px';
      this.$['sink-list'].style.maxHeight =
          this.dialogHeight_ - headerHeight - firstRunFlowHeight -
              issueHeight - searchHeight + 'px';
      var searchResults = this.$$('#search-results');
      if (searchResults) {
        searchResults.style.maxHeight = this.$['sink-list'].style.maxHeight;
      }
    });
  },

  /**
   * Update the max dialog height and update the positioning of the elements.
   *
   * @param {number} height The max height of the Media Router dialog.
   */
  updateMaxDialogHeight: function(height) {
    this.dialogHeight_ = height;
    this.updateElementPositioning_();
  },
});
