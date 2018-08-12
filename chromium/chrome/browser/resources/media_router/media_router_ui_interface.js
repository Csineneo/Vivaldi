// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// API invoked by the browser MediaRouterWebUIMessageHandler to communicate
// with this UI.
cr.define('media_router.ui', function() {
  'use strict';

  // The media-router-container element.
  var container = null;

  // The media-router-header element.
  var header = null;

  /**
   * Handles response of previous create route attempt.
   *
   * @param {string} sinkId The ID of the sink to which the Media Route was
   *     creating a route.
   * @param {?media_router.Route} route The newly created route that
   *     corresponds to the sink if route creation succeeded; null otherwise.
   * @param {boolean} isForDisplay Whether or not |route| is for display.
   */
  function onCreateRouteResponseReceived(sinkId, route, isForDisplay) {
    container.onCreateRouteResponseReceived(sinkId, route, isForDisplay);
  }

  /**
   * Sets the cast mode list.
   *
   * @param {!Array<!media_router.CastMode>} castModeList
   */
  function setCastModeList(castModeList) {
    container.castModeList = castModeList;
  }

  /**
   * Sets |container| and |header|.
   *
   * @param {!MediaRouterContainerElement} mediaRouterContainer
   * @param {!MediaRouterHeaderElement} mediaRouterHeader
   */
  function setElements(mediaRouterContainer, mediaRouterHeader) {
    container = mediaRouterContainer;
    header = mediaRouterHeader;
  }

  /**
   * Populates the WebUI with data obtained about the first run flow.
   *
   * @param {{firstRunFlowCloudPrefLearnMoreUrl: string,
   *          firstRunFlowLearnMoreUrl: string,
   *          wasFirstRunFlowAcknowledged: boolean,
   *          showFirstRunFlowCloudPref: boolean}} data
   * Parameters in data:
   *   firstRunFlowCloudPrefLearnMoreUrl - url to open when the cloud services
   *       pref learn more link is clicked.
   *   firstRunFlowLearnMoreUrl - url to open when the first run flow learn
   *       more link is clicked.
   *   wasFirstRunFlowAcknowledged - true if first run flow was previously
   *       acknowledged by user.
   *   showFirstRunFlowCloudPref - true if the cloud pref option should be
   *       shown.
   */
  function setFirstRunFlowData(data) {
    container.firstRunFlowCloudPrefLearnMoreUrl =
        data['firstRunFlowCloudPrefLearnMoreUrl'];
    container.firstRunFlowLearnMoreUrl =
        data['firstRunFlowLearnMoreUrl'];
    container.showFirstRunFlowCloudPref =
        data['showFirstRunFlowCloudPref'];
    // Some users acknowledged the first run flow before the cloud prefs
    // setting was implemented. These users will see the first run flow
    // again.
    container.showFirstRunFlow = !data['wasFirstRunFlowAcknowledged'] ||
        container.showFirstRunFlowCloudPref;
  }

  /**
   * Populates the WebUI with data obtained from Media Router.
   *
   * @param {{deviceMissingUrl: string,
   *          sinksAndIdentity: {
   *            sinks: !Array<!media_router.Sink>,
   *            showEmail: boolean,
   *            userEmail: string,
   *            showDomain: boolean,
   *            userDomain: string
   *          },
   *          routes: !Array<!media_router.Route>,
   *          castModes: !Array<!media_router.CastMode>}} data
   * Parameters in data:
   *   deviceMissingUrl - url to be opened on "Device missing?" clicked.
   *   sinksAndIdentity - list of sinks to be displayed and user identity.
   *   routes - list of routes that are associated with the sinks.
   *   castModes - list of available cast modes.
   */
  function setInitialData(data) {
    container.deviceMissingUrl = data['deviceMissingUrl'];
    container.castModeList = data['castModes'];
    this.setSinkListAndIdentity(data['sinksAndIdentity']);
    container.routeList = data['routes'];
    container.maybeShowRouteDetailsOnOpen();
    media_router.browserApi.onInitialDataReceived();
  }

  /**
   * Sets current issue to |issue|, or clears the current issue if |issue| is
   * null.
   *
   * @param {?media_router.Issue} issue
   */
  function setIssue(issue) {
    container.issue = issue;
  }

  /**
   * Sets the list of currently active routes.
   *
   * @param {!Array<!media_router.Route>} routeList
   */
  function setRouteList(routeList) {
    container.routeList = routeList;
  }

  /**
   * Sets the list of discovered sinks along with properties of whether to hide
   * identity of the user email and domain.
   *
   * @param {{sinks: !Array<!media_router.Sink>,
   *          showEmail: boolean,
   *          userEmail: string,
   *          showDomain: boolean,
   *          userDomain: string,}} data
   * Parameters in data:
   *   sinks - list of sinks to be displayed.
   *   showEmail - true if the user email should be shown.
   *   userEmail - email of the user if the user is signed in.
   *   showDomain - true if the user domain should be shown.
   *   userDomain - domain of the user if the user is signed in.
   */
  function setSinkListAndIdentity(data) {
    container.allSinks = data['sinks'];
    container.showDomain = data['showDomain'];
    container.userDomain = data['userDomain'];
    header.showEmail = data['showEmail'];
    header.userEmail = data['userEmail'];
  }

  /**
   * Updates the max height of the dialog
   *
   * @param {number} height
   */
  function updateMaxHeight(height) {
    container.updateMaxDialogHeight(height);
  }

  return {
    onCreateRouteResponseReceived: onCreateRouteResponseReceived,
    setCastModeList: setCastModeList,
    setElements: setElements,
    setFirstRunFlowData: setFirstRunFlowData,
    setInitialData: setInitialData,
    setIssue: setIssue,
    setRouteList: setRouteList,
    setSinkListAndIdentity: setSinkListAndIdentity,
    updateMaxHeight: updateMaxHeight,
  };
});

// API invoked by this UI to communicate with the browser WebUI message handler.
cr.define('media_router.browserApi', function() {
  'use strict';

  /**
   * Indicates that the user has acknowledged the first run flow.
   *
   * @param {boolean} optedIntoCloudServices Whether or not the user opted into
   *                  cloud services.
   */
  function acknowledgeFirstRunFlow(optedIntoCloudServices) {
    chrome.send('acknowledgeFirstRunFlow', [optedIntoCloudServices]);
  }

  /**
   * Acts on the given issue.
   *
   * @param {string} issueId
   * @param {number} actionType Type of action that the user clicked.
   * @param {?number} helpPageId The numeric help center ID.
   */
  function actOnIssue(issueId, actionType, helpPageId) {
    chrome.send('actOnIssue', [{issueId: issueId, actionType: actionType,
                                helpPageId: helpPageId}]);
  }

  /**
   * Closes the dialog.
   *
   * @param {boolean} pressEscToClose Whether the user pressed ESC to close the
   *                  dialog.
   */
  function closeDialog(pressEscToClose) {
    chrome.send('closeDialog', [pressEscToClose]);
  }

  /**
   * Closes the given route.
   *
   * @param {!media_router.Route} route
   */
  function closeRoute(route) {
    chrome.send('closeRoute', [{routeId: route.id, isLocal: route.isLocal}]);
  }

  /**
   * Joins the given route.
   *
   * @param {!media_router.Route} route
   */
  function joinRoute(route) {
    chrome.send('joinRoute', [{sinkId: route.sinkId, routeId: route.id}]);
  }

  /**
   * Indicates that the initial data has been received.
   */
  function onInitialDataReceived() {
    chrome.send('onInitialDataReceived');
  }

  /**
   * Reports when the user clicks outside the dialog.
   */
  function reportBlur() {
    chrome.send('reportBlur');
  }

  /**
   * Reports the index of the selected sink.
   *
   * @param {number} sinkIndex
   */
  function reportClickedSinkIndex(sinkIndex) {
    chrome.send('reportClickedSinkIndex', [sinkIndex]);
  }

  /**
   * Reports the initial dialog view.
   *
   * @param {string} view
   */
  function reportInitialState(view) {
    chrome.send('reportInitialState', [view]);
  }

  /**
   * Reports the initial action the user took.
   *
   * @param {number} action
   */
  function reportInitialAction(action) {
    chrome.send('reportInitialAction', [action]);
  }

  /**
   * Reports the navigation to the specified view.
   *
   * @param {string} view
   */
  function reportNavigateToView(view) {
    chrome.send('reportNavigateToView', [view]);
  }

  /**
   * Reports whether or not a route was created successfully.
   *
   * @param {boolean} success
   */
  function reportRouteCreation(success) {
    chrome.send('reportRouteCreation', [success]);
  }

  /**
   * Reports the cast mode that the user selected.
   *
   * @param {number} castModeType
   */
  function reportSelectedCastMode(castModeType) {
    chrome.send('reportSelectedCastMode', [castModeType]);
  }

  /**
   * Reports the current number of sinks.
   *
   * @param {number} sinkCount
   */
  function reportSinkCount(sinkCount) {
    chrome.send('reportSinkCount', [sinkCount]);
  }

  /**
   * Reports the time it took for the user to select a sink after the sink list
   * is populated and shown.
   *
   * @param {number} timeMs
   */
  function reportTimeToClickSink(timeMs) {
    chrome.send('reportTimeToClickSink', [timeMs]);
  }

  /**
   * Reports the time, in ms, it took for the user to close the dialog without
   * taking any other action.
   *
   * @param {number} timeMs
   */
  function reportTimeToInitialActionClose(timeMs) {
    chrome.send('reportTimeToInitialActionClose', [timeMs]);
  }

  /**
   * Requests data to initialize the WebUI with.
   * The data will be returned via media_router.ui.setInitialData.
   */
  function requestInitialData() {
    chrome.send('requestInitialData');
  }

  /**
   * Requests that a media route be started with the given sink.
   *
   * @param {string} sinkId The sink ID.
   * @param {number} selectedCastMode The value of the cast mode the user
   *   selected.
   */
  function requestRoute(sinkId, selectedCastMode) {
    chrome.send('requestRoute',
                [{sinkId: sinkId, selectedCastMode: selectedCastMode}]);
  }

  return {
    acknowledgeFirstRunFlow: acknowledgeFirstRunFlow,
    actOnIssue: actOnIssue,
    closeDialog: closeDialog,
    closeRoute: closeRoute,
    joinRoute: joinRoute,
    onInitialDataReceived: onInitialDataReceived,
    reportBlur: reportBlur,
    reportClickedSinkIndex: reportClickedSinkIndex,
    reportInitialAction: reportInitialAction,
    reportInitialState: reportInitialState,
    reportNavigateToView: reportNavigateToView,
    reportSelectedCastMode: reportSelectedCastMode,
    reportRouteCreation: reportRouteCreation,
    reportSinkCount: reportSinkCount,
    reportTimeToClickSink: reportTimeToClickSink,
    reportTimeToInitialActionClose: reportTimeToInitialActionClose,
    requestInitialData: requestInitialData,
    requestRoute: requestRoute,
  };
});
