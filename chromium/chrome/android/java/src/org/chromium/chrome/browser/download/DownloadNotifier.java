// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import android.content.Intent;

import org.chromium.content.browser.DownloadInfo;

/**
 * Class for reporting the status of a download.
 */
public interface DownloadNotifier {
    /**
     * Add a download successful notification.
     * @param downloadInfo info about the successful download.
     * @param intent Intent to launch when clicking the download notification.
     */
    void notifyDownloadSuccessful(DownloadInfo downloadInfo, Intent intent);

    /**
     * Add a download failed notification.
     * @param downloadInfo info about the failed download.
     */
    void notifyDownloadFailed(DownloadInfo downloadInfo);

    /**
     * Update the download progress notification.
     * @param downloadInfo info about in progress download.
     * @param startTimeInMillis the startTime of the download, measured in milliseconds, between the
     *        current time and midnight, January 1, 1970 UTC. Useful to keep progress notifications
     *        sorted by time.
     * @param canDownloadWhileMetered Wheter the download can take place on metered network.
     */
    void notifyDownloadProgress(
            DownloadInfo downloadInfo, long startTimeInMillis, boolean mCanDownloadWhileMetered);

    /**
     * Update the download notification to paused.
     * @param downloadInfo info about in progress download.
     * @param isAutoResumable Whether the download can be auto resumed when network is available.
     */
    void notifyDownloadPaused(DownloadInfo downloadInfo, boolean isAutoResumable);

    /**
     * Cancel the notification for a download.
     * @param notificationId The notification ID of the cancelled download.
     * @param downloadGuid The GUID of the cancelled download.
     */
    void cancelNotification(int notificationId, String downloadGuid);

    /**
     * Called to resume all the pending download entries in SharedPreferences.
     */
    void resumePendingDownloads();
}
