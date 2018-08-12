// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-people-page' is the settings page containing sign-in settings.
 */
Polymer({
  is: 'settings-people-page',

  behaviors: [
    WebUIListenerBehavior,
  ],

  properties: {
    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
    },

    /**
     * Preferences state.
     */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * The current sync status, supplied by SyncBrowserProxy.
     * @type {?settings.SyncStatus}
     */
    syncStatus: Object,

    /**
     * The currently selected profile icon URL. May be a data URL.
     */
    profileIconUrl_: String,

    /**
     * The current profile name.
     */
    profileName_: String,

    /** @private {!settings.SyncBrowserProxyImpl} */
    syncBrowserProxy_: {
      type: Object,
      value: function() {
        return settings.SyncBrowserProxyImpl.getInstance();
      },
    },

<if expr="chromeos">
    /**
     * True if Pin Unlock is allowed on this machine.
     */
    pinUnlockAllowed_: {
      type: Boolean,
      value: function() {
        /* TODO(jdufault): Return a real value via quickUnlockPrivate API. */
        return false;
      },
      readOnly: true,
    },

    /** @private {!settings.EasyUnlockBrowserProxyImpl} */
    easyUnlockBrowserProxy_: {
      type: Object,
      value: function() {
        return settings.EasyUnlockBrowserProxyImpl.getInstance();
      },
    },

    /**
     * True if Easy Unlock is allowed on this machine.
     */
    easyUnlockAllowed_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('easyUnlockAllowed');
      },
      readOnly: true,
    },

    /**
     * True if Easy Unlock is enabled.
     */
    easyUnlockEnabled_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('easyUnlockEnabled');
      },
    },

    /**
     * True if Easy Unlock's proximity detection feature is allowed.
     */
    easyUnlockProximityDetectionAllowed_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('easyUnlockAllowed') &&
            loadTimeData.getBoolean('easyUnlockProximityDetectionAllowed');
      },
      readOnly: true,
    },
</if>
  },

  /** @override */
  attached: function() {
    settings.ProfileInfoBrowserProxyImpl.getInstance().getProfileInfo().then(
        this.handleProfileInfo_.bind(this));
    this.addWebUIListener('profile-info-changed',
                          this.handleProfileInfo_.bind(this));

    this.syncBrowserProxy_.getSyncStatus().then(
        this.handleSyncStatus_.bind(this));
    this.addWebUIListener('sync-status-changed',
                          this.handleSyncStatus_.bind(this));

<if expr="chromeos">
    if (this.easyUnlockAllowed_) {
      this.addWebUIListener(
          'easy-unlock-enabled-status',
          this.handleEasyUnlockEnabledStatusChanged_.bind(this));
      this.easyUnlockBrowserProxy_.getEnabledStatus().then(
          this.handleEasyUnlockEnabledStatusChanged_.bind(this));
    }
</if>
  },

  /**
   * Handler for when the profile's icon and name is updated.
   * @private
   * @param {!settings.ProfileInfo} info
   */
  handleProfileInfo_: function(info) {
    this.profileName_ = info.name;
    this.profileIconUrl_ = info.iconUrl;
  },

  /**
   * Handler for when the sync state is pushed from the browser.
   * @param {?settings.SyncStatus} syncStatus
   * @private
   */
  handleSyncStatus_: function(syncStatus) {
    this.syncStatus = syncStatus;
  },

<if expr="chromeos">
  /**
   * Handler for when the Easy Unlock enabled status has changed.
   * @private
   */
  handleEasyUnlockEnabledStatusChanged_: function(easyUnlockEnabled) {
    this.easyUnlockEnabled_ = easyUnlockEnabled;
  },
</if>

  /** @private */
  onPictureTap_: function() {
<if expr="chromeos">
    this.$.pages.setSubpageChain(['changePicture']);
</if>
<if expr="not chromeos">
    this.$.pages.setSubpageChain(['manageProfile']);
</if>
  },

<if expr="not chromeos">
  /** @private */
  onProfileNameTap_: function() {
    this.$.pages.setSubpageChain(['manageProfile']);
  },
</if>

  /** @private */
  onActivityControlsTap_: function() {
    this.syncBrowserProxy_.openActivityControlsUrl();
  },

  /** @private */
  onSigninTap_: function() {
    this.syncBrowserProxy_.startSignIn();
  },

  /** @private */
  onDisconnectTap_: function() {
    this.$.disconnectDialog.open();
  },

  /** @private */
  onDisconnectConfirm_: function() {
    var deleteProfile = this.$.deleteProfile && this.$.deleteProfile.checked;
    this.syncBrowserProxy_.signOut(deleteProfile);

    // Dialog automatically closed because button has dialog-confirm attribute.
  },

  /** @private */
  onSyncTap_: function() {
    assert(this.syncStatus.signedIn);
    assert(this.syncStatus.syncSystemEnabled);

    if (this.syncStatus.managed)
      return;

    this.$.pages.setSubpageChain(['sync']);
  },

<if expr="chromeos">
  /** @private */
  onEasyUnlockSetupTap_: function() {
    this.easyUnlockBrowserProxy_.startTurnOnFlow();
  },

  /** @private */
  onEasyUnlockTurnOffTap_: function() {
    this.$$('#easyUnlockTurnOffDialog').open();
  },
</if>

  /** @private */
  onManageOtherPeople_: function() {
<if expr="not chromeos">
    this.syncBrowserProxy_.manageOtherPeople();
</if>
<if expr="chromeos">
    this.$.pages.setSubpageChain(['users']);
</if>
  },

  /**
   * @private
   * @param {?settings.SyncStatus} syncStatus
   * @return {boolean}
   */
  isAdvancedSyncSettingsVisible_: function(syncStatus) {
    return !!syncStatus && !!syncStatus.signedIn &&
        !!syncStatus.syncSystemEnabled;
  },

  /**
   * @private
   * @param {?settings.SyncStatus} syncStatus
   * @return {string}
   */
  getSyncIcon_: function(syncStatus) {
    if (!syncStatus)
      return '';
    if (syncStatus.hasError)
      return 'settings:sync-problem';
    if (syncStatus.managed)
      return 'settings:sync-disabled';

    return 'settings:done';
  },
});
