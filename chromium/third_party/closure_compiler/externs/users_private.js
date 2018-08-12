// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Externs generated from namespace: usersPrivate */

/**
 * @const
 */
chrome.usersPrivate = {};

/**
 * @typedef {{
 *   email: string,
 *   isOwner: boolean
 * }}
 * @see https://developer.chrome.com/extensions/usersPrivate#type-User
 */
var User;

/**
 * Gets a list of the currently whitelisted users.
 * @param {function(!Array<User>):void} callback
 * @see https://developer.chrome.com/extensions/usersPrivate#method-getWhitelistedUsers
 */
chrome.usersPrivate.getWhitelistedUsers = function(callback) {};

/**
 * Adds a new user with the given email to the whitelist. The callback is called
 * with true if the user was added succesfully, or with false if not (e.g.
 * because the user was already present, or the current user isn't the owner).
 * @param {string} email
 * @param {function(boolean):void} callback
 * @see https://developer.chrome.com/extensions/usersPrivate#method-addWhitelistedUser
 */
chrome.usersPrivate.addWhitelistedUser = function(email, callback) {};

/**
 * Removes the user with the given email from the whitelist. The callback is
 * called with true if the user was removed succesfully, or with false if not
 * (e.g. because the user was not already present, or the current user isn't the
 * owner).
 * @param {string} email
 * @param {function(boolean):void} callback
 * @see https://developer.chrome.com/extensions/usersPrivate#method-removeWhitelistedUser
 */
chrome.usersPrivate.removeWhitelistedUser = function(email, callback) {};

/**
 * Whether the current user is the owner of the device.
 * @param {function(boolean):void} callback
 * @see https://developer.chrome.com/extensions/usersPrivate#method-isCurrentUserOwner
 */
chrome.usersPrivate.isCurrentUserOwner = function(callback) {};

/**
 * Whether the whitelist is managed by enterprise.
 * @param {function(boolean):void} callback
 * @see https://developer.chrome.com/extensions/usersPrivate#method-isWhitelistManaged
 */
chrome.usersPrivate.isWhitelistManaged = function(callback) {};
