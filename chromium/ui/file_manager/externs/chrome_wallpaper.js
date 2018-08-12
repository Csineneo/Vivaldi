// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/**
 * TODO(ryoh): REMOVE this file if we decide how to define annotations for this
 * API.
 * @see https://codereview.chromium.org/1679983003/
 */
 /**
 * @const
 * @see https://goo.gl/7dvJFW
 */
chrome.wallpaper = {};

/**
 * Sets wallpaper to the image at url or wallpaperData with the specified
 * layout.
 * @param {{
 *    data: (ArrayBuffer|undefined),
 *    url: (string|undefined),
 *    layout: string,
 *    filename: string,
 *    thumbnail: (boolean|undefined)
 *  }} details
 * @param {function(ArrayBuffer=)} callback
 *
 */
 chrome.wallpaper.setWallpaper = function(details, callback) {};
