// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Behavior common to Site Settings classes.
 */

/** @polymerBehavior */
var SiteSettingsBehaviorImpl = {
  properties: {
    /**
     * The string ID of the category this element is displaying data for.
     * See site_settings/constants.js for possible values.
     */
    category: String,

    /**
     * The browser proxy used to retrieve and change information about site
     * settings categories and the sites within.
     * @type {settings.SiteSettingsPrefsBrowserProxy}
     */
    browserProxy: Object,
  },

  created: function() {
    this.browserProxy =
        settings.SiteSettingsPrefsBrowserProxyImpl.getInstance();
  },

  ready: function() {
    this.PermissionValues = settings.PermissionValues;
  },

  /**
   * A utility function to lookup a category name from its enum. Note: The
   * category name is visible to the user as part of the URL.
   * @param {string} category The category ID to look up.
   * @return {string} The category found or blank string if not found.
   * @protected
   */
  computeCategoryTextId: function(category) {
    switch (category) {
      case settings.ContentSettingsTypes.AUTOMATIC_DOWNLOADS:
        return 'automatic-downloads';
      case settings.ContentSettingsTypes.BACKGROUND_SYNC:
        return 'background-sync';
      case settings.ContentSettingsTypes.CAMERA:
        return 'camera';
      case settings.ContentSettingsTypes.COOKIES:
        return 'cookies';
      case settings.ContentSettingsTypes.GEOLOCATION:
        return 'location';
      case settings.ContentSettingsTypes.IMAGES:
        return 'images';
      case settings.ContentSettingsTypes.JAVASCRIPT:
        return 'javascript';
      case settings.ContentSettingsTypes.KEYGEN:
        return 'keygen';
      case settings.ContentSettingsTypes.MIC:
        return 'microphone';
      case settings.ContentSettingsTypes.NOTIFICATIONS:
        return 'notifications';
      case settings.ContentSettingsTypes.PLUGINS:
        return 'plugins';
      case settings.ContentSettingsTypes.POPUPS:
        return 'popups';
      case settings.ContentSettingsTypes.PROTOCOL_HANDLERS:
        return 'handlers';
      case settings.ContentSettingsTypes.UNSANDBOXED_PLUGINS:
        return 'unsandboxed-plugins';
      case settings.ContentSettingsTypes.USB_DEVICES:
        return 'usb-devices';
      case settings.ContentSettingsTypes.ZOOM_LEVELS:
        return 'zoom-levels';
      default:
        return '';
    }
  },

  /**
   * A utility function to lookup the route for a category name.
   * @param {string} category The category ID to look up.
   * @return {!settings.Route}
   * @protected
   */
  computeCategoryRoute: function(category) {
    switch (category) {
      case settings.ContentSettingsTypes.AUTOMATIC_DOWNLOADS:
        return settings.Route.SITE_SETTINGS_AUTOMATIC_DOWNLOADS;
      case settings.ContentSettingsTypes.BACKGROUND_SYNC:
        return settings.Route.SITE_SETTINGS_BACKGROUND_SYNC;
      case settings.ContentSettingsTypes.CAMERA:
        return settings.Route.SITE_SETTINGS_CAMERA;
      case settings.ContentSettingsTypes.COOKIES:
        return settings.Route.SITE_SETTINGS_COOKIES;
      case settings.ContentSettingsTypes.GEOLOCATION:
        return settings.Route.SITE_SETTINGS_LOCATION;
      case settings.ContentSettingsTypes.IMAGES:
        return settings.Route.SITE_SETTINGS_IMAGES;
      case settings.ContentSettingsTypes.JAVASCRIPT:
        return settings.Route.SITE_SETTINGS_JAVASCRIPT;
      case settings.ContentSettingsTypes.KEYGEN:
        return settings.Route.SITE_SETTINGS_KEYGEN;
      case settings.ContentSettingsTypes.MIC:
        return settings.Route.SITE_SETTINGS_MICROPHONE;
      case settings.ContentSettingsTypes.NOTIFICATIONS:
        return settings.Route.SITE_SETTINGS_NOTIFICATIONS;
      case settings.ContentSettingsTypes.PLUGINS:
        return settings.Route.SITE_SETTINGS_FLASH;
      case settings.ContentSettingsTypes.POPUPS:
        return settings.Route.SITE_SETTINGS_POPUPS;
      case settings.ContentSettingsTypes.PROTOCOL_HANDLERS:
        return settings.Route.SITE_SETTINGS_HANDLERS;
      case settings.ContentSettingsTypes.UNSANDBOXED_PLUGINS:
        return settings.Route.SITE_SETTINGS_UNSANDBOXED_PLUGINS;
      case settings.ContentSettingsTypes.USB_DEVICES:
        return settings.Route.SITE_SETTINGS_USB_DEVICES;
      case settings.ContentSettingsTypes.ZOOM_LEVELS:
        return settings.Route.SITE_SETTINGS_ZOOM_LEVELS;
    }
    assertNotReached();
  },

  /**
   * A utility function to compute the icon to use for the category, both for
   * the overall category as well as the individual permission in the details
   * for a site.
   * @param {string} category The category to show the icon for.
   * @return {string} The id of the icon for the given category.
   * @protected
   */
  computeIconForContentCategory: function(category) {
    switch (category) {
      case settings.ContentSettingsTypes.AUTOMATIC_DOWNLOADS:
        return 'cr:file-download';
      case settings.ContentSettingsTypes.BACKGROUND_SYNC:
        return 'settings:sync';
      case settings.ContentSettingsTypes.CAMERA:
        return 'settings:videocam';
      case settings.ContentSettingsTypes.COOKIES:
        return 'settings:cookie';
      case settings.ContentSettingsTypes.FULLSCREEN:
        return 'cr:fullscreen';
      case settings.ContentSettingsTypes.GEOLOCATION:
        return 'settings:location-on';
      case settings.ContentSettingsTypes.IMAGES:
        return 'settings:photo';
      case settings.ContentSettingsTypes.JAVASCRIPT:
        return 'settings:input';
      case settings.ContentSettingsTypes.KEYGEN:
        return 'settings:code';
      case settings.ContentSettingsTypes.MIC:
        return 'settings:mic';
      case settings.ContentSettingsTypes.NOTIFICATIONS:
        return 'settings:notifications';
      case settings.ContentSettingsTypes.PLUGINS:
        return 'cr:extension';
      case settings.ContentSettingsTypes.POPUPS:
        return 'cr:open-in-new';
      case settings.ContentSettingsTypes.PROTOCOL_HANDLERS:
        return 'settings:protocol-handler';
      case settings.ContentSettingsTypes.UNSANDBOXED_PLUGINS:
        return 'cr:extension';
      case settings.ContentSettingsTypes.USB_DEVICES:
        return 'settings:usb';
      case settings.ContentSettingsTypes.ZOOM_LEVELS:
        return 'settings:zoom-in';
      default:
        assertNotReached('Invalid category: ' + category);
        return '';
    }
  },

  /**
   * A utility function to compute the title of the category, both for
   * the overall category as well as the individual permission in the details
   * for a site.
   * @param {string} category The category to show the title for.
   * @return {string} The title for the given category.
   * @protected
   */
  computeTitleForContentCategory: function(category) {
    switch (category) {
      case settings.ContentSettingsTypes.AUTOMATIC_DOWNLOADS:
        return loadTimeData.getString('siteSettingsAutomaticDownloads');
      case settings.ContentSettingsTypes.BACKGROUND_SYNC:
        return loadTimeData.getString('siteSettingsBackgroundSync');
      case settings.ContentSettingsTypes.CAMERA:
        return loadTimeData.getString('siteSettingsCamera');
      case settings.ContentSettingsTypes.COOKIES:
        return loadTimeData.getString('siteSettingsCookies');
      case settings.ContentSettingsTypes.FULLSCREEN:
        return loadTimeData.getString('siteSettingsFullscreen');
      case settings.ContentSettingsTypes.GEOLOCATION:
        return loadTimeData.getString('siteSettingsLocation');
      case settings.ContentSettingsTypes.IMAGES:
        return loadTimeData.getString('siteSettingsImages');
      case settings.ContentSettingsTypes.JAVASCRIPT:
        return loadTimeData.getString('siteSettingsJavascript');
      case settings.ContentSettingsTypes.KEYGEN:
        return loadTimeData.getString('siteSettingsKeygen');
      case settings.ContentSettingsTypes.MIC:
        return loadTimeData.getString('siteSettingsMic');
      case settings.ContentSettingsTypes.NOTIFICATIONS:
        return loadTimeData.getString('siteSettingsNotifications');
      case settings.ContentSettingsTypes.PLUGINS:
        return loadTimeData.getString('siteSettingsFlash');
      case settings.ContentSettingsTypes.POPUPS:
        return loadTimeData.getString('siteSettingsPopups');
      case settings.ContentSettingsTypes.PROTOCOL_HANDLERS:
        return loadTimeData.getString('siteSettingsHandlers');
      case settings.ContentSettingsTypes.UNSANDBOXED_PLUGINS:
        return loadTimeData.getString('siteSettingsUnsandboxedPlugins');
      case settings.ContentSettingsTypes.USB_DEVICES:
        return loadTimeData.getString('siteSettingsUsbDevices');
      case settings.ContentSettingsTypes.ZOOM_LEVELS:
        return loadTimeData.getString('siteSettingsZoomLevels');
      default:
        assertNotReached('Invalid category: ' + category);
        return '';
    }
  },

  /**
   * A utility function to compute the description for the category.
   * @param {string} category The category to show the description for.
   * @param {string} setting The string value of the setting.
   * @param {boolean} showRecommendation Whether to show the '(recommended)'
   *     label prefix.
   * @return {string} The category description.
   * @protected
   */
  computeCategoryDesc: function(category, setting, showRecommendation) {
    var categoryEnabled = this.computeIsSettingEnabled(category, setting);
    switch (category) {
      case settings.ContentSettingsTypes.JAVASCRIPT:
        // "Allowed (recommended)" vs "Blocked".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsBlocked');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsAllowedRecommended') :
            loadTimeData.getString('siteSettingsAllowed');
      case settings.ContentSettingsTypes.POPUPS:
        // "Allowed" vs "Blocked (recommended)".
        if (categoryEnabled) {
          return loadTimeData.getString('siteSettingsAllowed');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsBlockedRecommended') :
            loadTimeData.getString('siteSettingsBlocked');
      case settings.ContentSettingsTypes.NOTIFICATIONS:
        // "Ask before sending (recommended)" vs "Blocked".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsBlocked');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsAskBeforeSendingRecommended') :
            loadTimeData.getString('siteSettingsAskBeforeSending');
      case settings.ContentSettingsTypes.CAMERA:
      case settings.ContentSettingsTypes.GEOLOCATION:
      case settings.ContentSettingsTypes.MIC:
        // "Ask before accessing (recommended)" vs "Blocked".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsBlocked');
        }
        return showRecommendation ?
            loadTimeData.getString(
                'siteSettingsAskBeforeAccessingRecommended') :
            loadTimeData.getString('siteSettingsAskBeforeAccessing');
      case settings.ContentSettingsTypes.COOKIES:
        // Tri-state: "Allow sites to save and read cookie data" vs "Blocked"
        //     vs "Keep local data only until you quit your browser".
        if (setting == settings.PermissionValues.BLOCK)
          return loadTimeData.getString('siteSettingsBlocked');
        if (setting == settings.PermissionValues.SESSION_ONLY)
          return loadTimeData.getString('deleteDataPostSession');
        return showRecommendation ?
            loadTimeData.getString('siteSettingsCookiesAllowedRecommended') :
            loadTimeData.getString('siteSettingsCookiesAllowed');
      case settings.ContentSettingsTypes.PROTOCOL_HANDLERS:
        // "Allow sites to ask to become default handlers" vs "Blocked".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsHandlersBlocked');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsHandlersAskRecommended') :
            loadTimeData.getString('siteSettingsHandlersAsk');
      case settings.ContentSettingsTypes.IMAGES:
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsDontShowImages');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsShowAllRecommended') :
            loadTimeData.getString('siteSettingsShowAll');
      case settings.ContentSettingsTypes.PLUGINS:
        if (setting == settings.PermissionValues.ALLOW)
          return loadTimeData.getString('siteSettingsFlashAllow');
        if (setting == settings.PermissionValues.BLOCK)
          return loadTimeData.getString('siteSettingsFlashBlock');
        return loadTimeData.getString('siteSettingsFlashAskBefore');
      case settings.ContentSettingsTypes.BACKGROUND_SYNC:
        // "Allow sites to finish sending and receiving data" vs "Do not allow".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsBackgroundSyncBlocked');
        }
        return showRecommendation ?
            loadTimeData.getString(
                 'siteSettingsAllowRecentlyClosedSitesRecommended') :
            loadTimeData.getString('siteSettingsAllowRecentlyClosedSites');
      case settings.ContentSettingsTypes.KEYGEN:
        // "Allow sites to use keygen" vs "Do not allow".
        if (categoryEnabled) {
          return loadTimeData.getString('siteSettingsKeygenAllow');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsKeygenBlockRecommended') :
            loadTimeData.getString('siteSettingsKeygenBlock');
      case settings.ContentSettingsTypes.AUTOMATIC_DOWNLOADS:
        // "Ask when a site wants to auto-download multiple" vs "Do not allow".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsAutoDownloadBlock');
        }
        return showRecommendation ?
            loadTimeData.getString('siteSettingsAutoDownloadAskRecommended') :
            loadTimeData.getString('siteSettingsAutoDownloadAsk');
      case settings.ContentSettingsTypes.UNSANDBOXED_PLUGINS:
        // "Ask when a plugin accesses your computer" vs "Do not allow".
        if (!categoryEnabled) {
          return loadTimeData.getString('siteSettingsUnsandboxedPluginsBlock');
        }
        return showRecommendation ?
            loadTimeData.getString(
                'siteSettingsUnsandboxedPluginsAskRecommended') :
            loadTimeData.getString('siteSettingsUnsandboxedPluginsAsk');
      default:
        assertNotReached('Invalid category: ' + category);
        return '';
    }
  },

  /**
   * Ensures the URL has a scheme (assumes http if omitted).
   * @param {string} url The URL with or without a scheme.
   * @return {string} The URL with a scheme, or an empty string.
   */
  ensureUrlHasScheme: function(url) {
    if (url.length == 0) return url;
    return url.includes('://') ? url : 'http://' + url;
  },

  /**
   * Removes redundant ports, such as port 80 for http and 443 for https.
   * @param {string} url The URL to sanitize.
   * @return {string} The URL without redundant ports, if any.
   */
  sanitizePort: function(url) {
    var urlWithScheme = this.ensureUrlHasScheme(url);
    if (urlWithScheme.startsWith('https://') &&
        urlWithScheme.endsWith(':443')) {
      return url.slice(0, -4);
    }
    if (urlWithScheme.startsWith('http://') &&
        urlWithScheme.endsWith(':80')) {
      return url.slice(0, -3);
    }
    return url;
  },

  /**
   * Adds the wildcard prefix to a pattern string (if missing).
   * @param {string} pattern The pattern to add the wildcard to.
   * @return {string} The resulting pattern.
   * @private
   */
  addPatternWildcard: function(pattern) {
    if (pattern.indexOf('[*.]') > -1)
      return pattern;
    if (pattern.startsWith('http://'))
      return pattern.replace('http://', 'http://[*.]');
    else if (pattern.startsWith('https://'))
      return pattern.replace('https://', 'https://[*.]');
    else if (pattern.startsWith('chrome-extension://'))
      return pattern;  // No need for a wildcard for this.
    else
      return '[*.]' + pattern;
  },

  /**
   * Removes the wildcard prefix from a pattern string.
   * @param {string} pattern The pattern to remove the wildcard from.
   * @return {string} The resulting pattern.
   * @private
   */
  removePatternWildcard: function(pattern) {
    if (pattern.startsWith('http://[*.]'))
      return pattern.replace('http://[*.]', 'http://');
    else if (pattern.startsWith('https://[*.]'))
      return pattern.replace('https://[*.]', 'https://');
    else if (pattern.startsWith('[*.]'))
      return pattern.substring(4, pattern.length);
    return pattern;
  },

  /**
   * Looks up the human-friendly embedder string to show in the UI.
   * @param {string} embeddingOrigin The embedding origin to show.
   * @param {string} category The category requesting it.
   * @return {string} The string to show.
   */
  getEmbedderString: function(embeddingOrigin, category) {
    if (embeddingOrigin == '') {
      if (category != settings.ContentSettingsTypes.GEOLOCATION)
        return '';
      return loadTimeData.getStringF('embeddedOnHost', '*');
    }
    return loadTimeData.getStringF(
        'embeddedOnHost', this.sanitizePort(embeddingOrigin));
  },

  /**
   * Returns true if this exception is controlled by, for example, a policy or
   * set by an extension.
   * @param {string} source The source controlling the extension
   * @return {boolean} Whether it is being controlled.
   * @protected
   */
  isExceptionControlled_: function(source) {
    return source != undefined && source != 'preference';
  },

  /**
   * Returns the icon to use for a given site.
   * @param {string} site The url of the site to fetch the icon for.
   * @return {string} The background-image style with the favicon.
   * @private
   */
  computeSiteIcon: function(site) {
    var url = this.ensureUrlHasScheme(site);
    return 'background-image: ' + cr.icon.getFavicon(url);
  },

  /**
   * Returns true if the passed content setting is considered 'enabled'.
   * @param {string} category
   * @param {string} setting
   * @return {boolean}
   * @private
   */
  computeIsSettingEnabled: function(category, setting) {
    // FullScreen is Allow vs. Ask.
    return category == settings.ContentSettingsTypes.FULLSCREEN ?
        setting != settings.PermissionValues.ASK :
        setting != settings.PermissionValues.BLOCK;
  },

  /**
   * Converts a string origin/pattern to a URL.
   * @param {string} originOrPattern The origin/pattern to convert to URL.
   * @return {URL} The URL to return (or null if origin is not a valid URL).
   * @private
   */
  toUrl: function(originOrPattern) {
    if (originOrPattern.length == 0)
      return null;
    // TODO(finnur): Hmm, it would probably be better to ensure scheme on the
    //     JS/C++ boundary.
    // TODO(dschuyler): I agree. This filtering should be done in one go, rather
    // that during the sort. The URL generation should be wrapped in a try/catch
    // as well.
    originOrPattern = originOrPattern.replace('*://', '');
    originOrPattern = originOrPattern.replace('[*.]', '');
    return new URL(this.ensureUrlHasScheme(originOrPattern));
  },

  /**
   * Convert an exception (received from the C++ handler) to a full
   * SiteException.
   * @param {!Object} exception The raw site exception from C++.
   * @return {SiteException} The expanded (full) SiteException.
   * @private
   */
  expandSiteException: function(exception) {
    var origin = exception.origin;
    var url = this.toUrl(origin);
    var originForDisplay = url ? this.sanitizePort(url.origin) : origin;

    var embeddingOrigin = exception.embeddingOrigin;
    var embeddingOriginForDisplay = '';
    if (origin != embeddingOrigin) {
      embeddingOriginForDisplay =
          this.getEmbedderString(embeddingOrigin, this.category);
    }

    return {
      origin: origin,
      originForDisplay: originForDisplay,
      embeddingOrigin: embeddingOrigin,
      embeddingOriginForDisplay: embeddingOriginForDisplay,
      incognito: exception.incognito,
      setting: exception.setting,
      source: exception.source,
    };
  },

};

/** @polymerBehavior */
var SiteSettingsBehavior = [SiteSettingsBehaviorImpl];
