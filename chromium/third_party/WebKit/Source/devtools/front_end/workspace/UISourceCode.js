/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @implements {Common.ContentProvider}
 * @unrestricted
 */
Workspace.UISourceCode = class extends Common.Object {
  /**
   * @param {!Workspace.Project} project
   * @param {string} url
   * @param {!Common.ResourceType} contentType
   */
  constructor(project, url, contentType) {
    super();
    this._project = project;
    this._url = url;

    var parsedURL = url.asParsedURL();
    if (parsedURL) {
      this._origin = parsedURL.securityOrigin();
      this._parentURL = this._origin + parsedURL.folderPathComponents;
      this._name = parsedURL.lastPathComponent;
      if (parsedURL.queryParams)
        this._name += '?' + parsedURL.queryParams;
    } else {
      this._origin = '';
      this._parentURL = '';
      this._name = url;
    }

    this._contentType = contentType;
    /** @type {?function(?string)} */
    this._requestContentCallback = null;
    /** @type {?Promise<?string>} */
    this._requestContentPromise = null;
    /** @type {!Map<string, !Map<number, !Workspace.UISourceCode.LineMarker>>} */
    this._lineDecorations = new Map();

    /** @type {!Array.<!Workspace.Revision>} */
    this.history = [];

    /** @type {!Array<!Workspace.UISourceCode.Message>} */
    this._messages = [];
  }

  /**
   * @return {!Promise<?Workspace.UISourceCodeMetadata>}
   */
  requestMetadata() {
    return this._project.requestMetadata(this);
  }

  /**
   * @return {string}
   */
  name() {
    return this._name;
  }

  /**
   * @return {string}
   */
  url() {
    return this._url;
  }

  /**
   * @return {string}
   */
  parentURL() {
    return this._parentURL;
  }

  /**
   * @return {string}
   */
  origin() {
    return this._origin;
  }

  /**
   * @return {string}
   */
  fullDisplayName() {
    var parentPath = this._parentURL.replace(/^(?:https?|file)\:\/\//, '');
    try {
      parentPath = decodeURI(parentPath);
    } catch (e) {
    }
    return parentPath + '/' + this.displayName(true);
  }

  /**
   * @param {boolean=} skipTrim
   * @return {string}
   */
  displayName(skipTrim) {
    if (!this._name)
      return Common.UIString('(index)');
    var name = this._name;
    try {
      name = decodeURI(name);
    } catch (e) {
    }
    return skipTrim ? name : name.trimEnd(100);
  }

  /**
   * @return {boolean}
   */
  isFromServiceProject() {
    return Workspace.Project.isServiceProject(this._project);
  }

  /**
   * @return {boolean}
   */
  canRename() {
    return this._project.canRename();
  }

  /**
   * @param {string} newName
   * @param {function(boolean)} callback
   */
  rename(newName, callback) {
    this._project.rename(this, newName, innerCallback.bind(this));

    /**
     * @param {boolean} success
     * @param {string=} newName
     * @param {string=} newURL
     * @param {!Common.ResourceType=} newContentType
     * @this {Workspace.UISourceCode}
     */
    function innerCallback(success, newName, newURL, newContentType) {
      if (success) {
        this._updateName(
            /** @type {string} */ (newName), /** @type {string} */ (newURL),
            /** @type {!Common.ResourceType} */ (newContentType));
      }
      callback(success);
    }
  }

  remove() {
    this._project.deleteFile(this.url());
  }

  /**
   * @param {string} name
   * @param {string} url
   * @param {!Common.ResourceType=} contentType
   */
  _updateName(name, url, contentType) {
    var oldURL = this.url();
    this._url = this._url.substring(0, this._url.length - this._name.length) + name;
    this._name = name;
    if (url)
      this._url = url;
    if (contentType)
      this._contentType = contentType;
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.TitleChanged, oldURL);
  }

  /**
   * @override
   * @return {string}
   */
  contentURL() {
    return this.url();
  }

  /**
   * @override
   * @return {!Common.ResourceType}
   */
  contentType() {
    return this._contentType;
  }

  /**
   * @return {!Workspace.Project}
   */
  project() {
    return this._project;
  }

  /**
   * @override
   * @return {!Promise<?string>}
   */
  requestContent() {
    if (this._content || this._contentLoaded)
      return Promise.resolve(this._content);
    var promise = this._requestContentPromise;
    if (!promise) {
      promise = new Promise(fulfill => this._requestContentCallback = fulfill);
      this._requestContentPromise = promise;
      this._project.requestFileContent(this, this._fireContentAvailable.bind(this));
    }
    return promise;
  }

  /**
   * @param {function()} callback
   */
  _pushCheckContentUpdatedCallback(callback) {
    if (!this._checkContentUpdatedCallbacks)
      this._checkContentUpdatedCallbacks = [];
    this._checkContentUpdatedCallbacks.push(callback);
  }

  _terminateContentCheck() {
    delete this._checkingContent;
    if (this._checkContentUpdatedCallbacks) {
      this._checkContentUpdatedCallbacks.forEach(function(callback) {
        callback();
      });
      delete this._checkContentUpdatedCallbacks;
    }
  }

  /**
   * @param {boolean=} forceLoad
   * @param {function()=} callback
   */
  checkContentUpdated(forceLoad, callback) {
    callback = callback || function() {};
    forceLoad = forceLoad || this._forceLoadOnCheckContent;
    if (!this.contentLoaded() && !forceLoad) {
      callback();
      return;
    }

    if (!this._project.canSetFileContent()) {
      callback();
      return;
    }
    this._pushCheckContentUpdatedCallback(callback);

    if (this._checkingContent)
      return;

    this._checkingContent = true;
    this._project.requestFileContent(this, contentLoaded.bind(this));

    /**
     * @param {?string} updatedContent
     * @this {Workspace.UISourceCode}
     */
    function contentLoaded(updatedContent) {
      if (updatedContent === null) {
        var workingCopy = this.workingCopy();
        this._contentCommitted('', false);
        this.setWorkingCopy(workingCopy);
        this._terminateContentCheck();
        return;
      }
      if (typeof this._lastAcceptedContent === 'string' && this._lastAcceptedContent === updatedContent) {
        this._terminateContentCheck();
        return;
      }

      if (this._content === updatedContent) {
        delete this._lastAcceptedContent;
        this._terminateContentCheck();
        return;
      }

      if (!this.isDirty() || this._workingCopy === updatedContent) {
        this._contentCommitted(updatedContent, false);
        this._terminateContentCheck();
        return;
      }

      var shouldUpdate =
          window.confirm(Common.UIString('This file was changed externally. Would you like to reload it?'));
      if (shouldUpdate)
        this._contentCommitted(updatedContent, false);
      else
        this._lastAcceptedContent = updatedContent;
      this._terminateContentCheck();
    }
  }

  forceLoadOnCheckContent() {
    this._forceLoadOnCheckContent = true;
  }

  /**
   * @return {!Promise<?string>}
   */
  requestOriginalContent() {
    var callback;
    var promise = new Promise(fulfill => callback = fulfill);
    this._project.requestFileContent(this, callback);
    return promise;
  }

  /**
   * @param {string} content
   */
  _commitContent(content) {
    if (this._project.canSetFileContent()) {
      this._project.setFileContent(this, content, function() {});
    } else if (this._url && Workspace.fileManager.isURLSaved(this._url)) {
      Workspace.fileManager.save(this._url, content, false, function() {});
      Workspace.fileManager.close(this._url);
    }
    this._contentCommitted(content, true);
  }

  /**
   * @param {string} content
   * @param {boolean} committedByUser
   */
  _contentCommitted(content, committedByUser) {
    delete this._lastAcceptedContent;
    this._content = content;
    this._contentLoaded = true;

    var lastRevision = this.history.length ? this.history[this.history.length - 1] : null;
    if (!lastRevision || lastRevision._content !== this._content) {
      var revision = new Workspace.Revision(this, this._content, new Date());
      this.history.push(revision);
    }

    this._innerResetWorkingCopy();
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.WorkingCopyCommitted, {content: content});
    this._project.workspace().dispatchEventToListeners(
        Workspace.Workspace.Events.WorkingCopyCommitted, {uiSourceCode: this, content: content});
    if (committedByUser) {
      this._project.workspace().dispatchEventToListeners(
          Workspace.Workspace.Events.WorkingCopyCommittedByUser, {uiSourceCode: this, content: content});
    }
  }

  saveAs() {
    Workspace.fileManager.save(this._url, this.workingCopy(), true, callback.bind(this));
    Workspace.fileManager.close(this._url);

    /**
     * @param {boolean} accepted
     * @this {Workspace.UISourceCode}
     */
    function callback(accepted) {
      if (accepted)
        this._contentCommitted(this.workingCopy(), true);
    }
  }

  /**
   * @param {string} content
   */
  addRevision(content) {
    this._commitContent(content);
  }

  /**
   * @return {!Promise}
   */
  revertToOriginal() {
    /**
     * @this {Workspace.UISourceCode}
     * @param {?string} content
     */
    function callback(content) {
      if (typeof content !== 'string')
        return;

      this.addRevision(content);
    }

    Host.userMetrics.actionTaken(Host.UserMetrics.Action.RevisionApplied);
    return this.requestOriginalContent().then(callback.bind(this));
  }

  /**
   * @param {function(!Workspace.UISourceCode)} callback
   */
  revertAndClearHistory(callback) {
    /**
     * @this {Workspace.UISourceCode}
     * @param {?string} content
     */
    function revert(content) {
      if (typeof content !== 'string')
        return;

      this.addRevision(content);
      this.history = [];
      callback(this);
    }

    Host.userMetrics.actionTaken(Host.UserMetrics.Action.RevisionApplied);
    this.requestOriginalContent().then(revert.bind(this));
  }

  /**
   * @return {string}
   */
  workingCopy() {
    if (this._workingCopyGetter) {
      this._workingCopy = this._workingCopyGetter();
      delete this._workingCopyGetter;
    }
    if (this.isDirty())
      return this._workingCopy;
    return this._content;
  }

  resetWorkingCopy() {
    this._innerResetWorkingCopy();
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.WorkingCopyChanged);
  }

  _innerResetWorkingCopy() {
    delete this._workingCopy;
    delete this._workingCopyGetter;
  }

  /**
   * @param {string} newWorkingCopy
   */
  setWorkingCopy(newWorkingCopy) {
    this._workingCopy = newWorkingCopy;
    delete this._workingCopyGetter;
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.WorkingCopyChanged);
    this._project.workspace().dispatchEventToListeners(
        Workspace.Workspace.Events.WorkingCopyChanged, {uiSourceCode: this});
  }

  setWorkingCopyGetter(workingCopyGetter) {
    this._workingCopyGetter = workingCopyGetter;
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.WorkingCopyChanged);
    this._project.workspace().dispatchEventToListeners(
        Workspace.Workspace.Events.WorkingCopyChanged, {uiSourceCode: this});
  }

  removeWorkingCopyGetter() {
    if (!this._workingCopyGetter)
      return;
    this._workingCopy = this._workingCopyGetter();
    delete this._workingCopyGetter;
  }

  commitWorkingCopy() {
    if (this.isDirty())
      this._commitContent(this.workingCopy());
  }

  /**
   * @return {boolean}
   */
  isDirty() {
    return typeof this._workingCopy !== 'undefined' || typeof this._workingCopyGetter !== 'undefined';
  }

  /**
   * @return {string}
   */
  extension() {
    return Common.ParsedURL.extractExtension(this._name);
  }

  /**
   * @return {?string}
   */
  content() {
    return this._content;
  }

  /**
   * @override
   * @param {string} query
   * @param {boolean} caseSensitive
   * @param {boolean} isRegex
   * @param {function(!Array.<!Common.ContentProvider.SearchMatch>)} callback
   */
  searchInContent(query, caseSensitive, isRegex, callback) {
    var content = this.content();
    if (!content) {
      this._project.searchInFileContent(this, query, caseSensitive, isRegex, callback);
      return;
    }

    // searchInContent should call back later.
    setTimeout(doSearch.bind(null, content), 0);

    /**
     * @param {string} content
     */
    function doSearch(content) {
      callback(Common.ContentProvider.performSearchInContent(content, query, caseSensitive, isRegex));
    }
  }

  /**
   * @param {?string} content
   */
  _fireContentAvailable(content) {
    this._contentLoaded = true;
    this._content = content;

    var callback = this._requestContentCallback;
    this._requestContentCallback = null;
    this._requestContentPromise = null;

    callback.call(null, content);
  }

  /**
   * @return {boolean}
   */
  contentLoaded() {
    return this._contentLoaded;
  }

  /**
   * @param {number} lineNumber
   * @param {number=} columnNumber
   * @return {!Workspace.UILocation}
   */
  uiLocation(lineNumber, columnNumber) {
    if (typeof columnNumber === 'undefined')
      columnNumber = 0;
    return new Workspace.UILocation(this, lineNumber, columnNumber);
  }

  /**
   * @return {!Array<!Workspace.UISourceCode.Message>}
   */
  messages() {
    return this._messages.slice();
  }

  /**
   * @param {!Workspace.UISourceCode.Message.Level} level
   * @param {string} text
   * @param {number} lineNumber
   * @param {number=} columnNumber
   * @return {!Workspace.UISourceCode.Message} message
   */
  addLineMessage(level, text, lineNumber, columnNumber) {
    return this.addMessage(
        level, text, new Common.TextRange(lineNumber, columnNumber || 0, lineNumber, columnNumber || 0));
  }

  /**
   * @param {!Workspace.UISourceCode.Message.Level} level
   * @param {string} text
   * @param {!Common.TextRange} range
   * @return {!Workspace.UISourceCode.Message} message
   */
  addMessage(level, text, range) {
    var message = new Workspace.UISourceCode.Message(this, level, text, range);
    this._messages.push(message);
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.MessageAdded, message);
    return message;
  }

  /**
   * @param {!Workspace.UISourceCode.Message} message
   */
  removeMessage(message) {
    if (this._messages.remove(message))
      this.dispatchEventToListeners(Workspace.UISourceCode.Events.MessageRemoved, message);
  }

  removeAllMessages() {
    var messages = this._messages;
    this._messages = [];
    for (var message of messages)
      this.dispatchEventToListeners(Workspace.UISourceCode.Events.MessageRemoved, message);
  }

  /**
   * @param {number} lineNumber
   * @param {string} type
   * @param {?} data
   */
  addLineDecoration(lineNumber, type, data) {
    var markers = this._lineDecorations.get(type);
    if (!markers) {
      markers = new Map();
      this._lineDecorations.set(type, markers);
    }
    var marker = new Workspace.UISourceCode.LineMarker(lineNumber, type, data);
    markers.set(lineNumber, marker);
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.LineDecorationAdded, marker);
  }

  /**
   * @param {number} lineNumber
   * @param {string} type
   */
  removeLineDecoration(lineNumber, type) {
    var markers = this._lineDecorations.get(type);
    if (!markers)
      return;
    var marker = markers.get(lineNumber);
    if (!marker)
      return;
    markers.delete(lineNumber);
    this.dispatchEventToListeners(Workspace.UISourceCode.Events.LineDecorationRemoved, marker);
    if (!markers.size)
      this._lineDecorations.delete(type);
  }

  /**
   * @param {string} type
   */
  removeAllLineDecorations(type) {
    var markers = this._lineDecorations.get(type);
    if (!markers)
      return;
    this._lineDecorations.delete(type);
    markers.forEach(marker => {
      this.dispatchEventToListeners(Workspace.UISourceCode.Events.LineDecorationRemoved, marker);
    });
  }

  /**
   * @param {string} type
   * @return {?Map<number, !Workspace.UISourceCode.LineMarker>}
   */
  lineDecorations(type) {
    return this._lineDecorations.get(type) || null;
  }
};

/** @enum {symbol} */
Workspace.UISourceCode.Events = {
  WorkingCopyChanged: Symbol('WorkingCopyChanged'),
  WorkingCopyCommitted: Symbol('WorkingCopyCommitted'),
  TitleChanged: Symbol('TitleChanged'),
  SourceMappingChanged: Symbol('SourceMappingChanged'),
  MessageAdded: Symbol('MessageAdded'),
  MessageRemoved: Symbol('MessageRemoved'),
  LineDecorationAdded: Symbol('LineDecorationAdded'),
  LineDecorationRemoved: Symbol('LineDecorationRemoved')
};

/**
 * @unrestricted
 */
Workspace.UILocation = class {
  /**
   * @param {!Workspace.UISourceCode} uiSourceCode
   * @param {number} lineNumber
   * @param {number} columnNumber
   */
  constructor(uiSourceCode, lineNumber, columnNumber) {
    this.uiSourceCode = uiSourceCode;
    this.lineNumber = lineNumber;
    this.columnNumber = columnNumber;
  }

  /**
   * @return {string}
   */
  linkText() {
    var linkText = this.uiSourceCode.displayName();
    if (typeof this.lineNumber === 'number')
      linkText += ':' + (this.lineNumber + 1);
    return linkText;
  }

  /**
   * @return {string}
   */
  id() {
    return this.uiSourceCode.project().id() + ':' + this.uiSourceCode.url() + ':' + this.lineNumber + ':' +
        this.columnNumber;
  }

  /**
   * @return {string}
   */
  toUIString() {
    return this.uiSourceCode.url() + ':' + (this.lineNumber + 1);
  }

  /**
   * @param {!Workspace.UILocation} location1
   * @param {!Workspace.UILocation} location2
   * @return {number}
   */
  static comparator(location1, location2) {
    return location1.compareTo(location2);
  }

  /**
   * @param {!Workspace.UILocation} other
   * @return {number}
   */
  compareTo(other) {
    if (this.uiSourceCode.url() !== other.uiSourceCode.url())
      return this.uiSourceCode.url() > other.uiSourceCode.url() ? 1 : -1;
    if (this.lineNumber !== other.lineNumber)
      return this.lineNumber - other.lineNumber;
    return this.columnNumber - other.columnNumber;
  }
};

/**
 * @implements {Common.ContentProvider}
 * @unrestricted
 */
Workspace.Revision = class {
  /**
   * @param {!Workspace.UISourceCode} uiSourceCode
   * @param {?string|undefined} content
   * @param {!Date} timestamp
   */
  constructor(uiSourceCode, content, timestamp) {
    this._uiSourceCode = uiSourceCode;
    this._content = content;
    this._timestamp = timestamp;
  }

  /**
   * @return {!Workspace.UISourceCode}
   */
  get uiSourceCode() {
    return this._uiSourceCode;
  }

  /**
   * @return {!Date}
   */
  get timestamp() {
    return this._timestamp;
  }

  /**
   * @return {?string}
   */
  get content() {
    return this._content || null;
  }

  /**
   * @return {!Promise}
   */
  revertToThis() {
    /**
     * @param {?string} content
     * @this {Workspace.Revision}
     */
    function revert(content) {
      if (content && this._uiSourceCode._content !== content)
        this._uiSourceCode.addRevision(content);
    }
    Host.userMetrics.actionTaken(Host.UserMetrics.Action.RevisionApplied);
    return this.requestContent().then(revert.bind(this));
  }

  /**
   * @override
   * @return {string}
   */
  contentURL() {
    return this._uiSourceCode.url();
  }

  /**
   * @override
   * @return {!Common.ResourceType}
   */
  contentType() {
    return this._uiSourceCode.contentType();
  }

  /**
   * @override
   * @return {!Promise<?string>}
   */
  requestContent() {
    return Promise.resolve(/** @type {?string} */ (this._content || ''));
  }

  /**
   * @override
   * @param {string} query
   * @param {boolean} caseSensitive
   * @param {boolean} isRegex
   * @param {function(!Array.<!Common.ContentProvider.SearchMatch>)} callback
   */
  searchInContent(query, caseSensitive, isRegex, callback) {
    callback([]);
  }
};

/**
 * @unrestricted
 */
Workspace.UISourceCode.Message = class {
  /**
   * @param {!Workspace.UISourceCode} uiSourceCode
   * @param {!Workspace.UISourceCode.Message.Level} level
   * @param {string} text
   * @param {!Common.TextRange} range
   */
  constructor(uiSourceCode, level, text, range) {
    this._uiSourceCode = uiSourceCode;
    this._level = level;
    this._text = text;
    this._range = range;
  }

  /**
   * @return {!Workspace.UISourceCode}
   */
  uiSourceCode() {
    return this._uiSourceCode;
  }

  /**
   * @return {!Workspace.UISourceCode.Message.Level}
   */
  level() {
    return this._level;
  }

  /**
   * @return {string}
   */
  text() {
    return this._text;
  }

  /**
   * @return {!Common.TextRange}
   */
  range() {
    return this._range;
  }

  /**
   * @return {number}
   */
  lineNumber() {
    return this._range.startLine;
  }

  /**
   * @return {(number|undefined)}
   */
  columnNumber() {
    return this._range.startColumn;
  }

  /**
   * @param {!Workspace.UISourceCode.Message} another
   * @return {boolean}
   */
  isEqual(another) {
    return this._uiSourceCode === another._uiSourceCode && this.text() === another.text() &&
        this.level() === another.level() && this.range().equal(another.range());
  }

  remove() {
    this._uiSourceCode.removeMessage(this);
  }
};

/**
 * @enum {string}
 */
Workspace.UISourceCode.Message.Level = {
  Error: 'Error',
  Warning: 'Warning'
};

/**
 * @unrestricted
 */
Workspace.UISourceCode.LineMarker = class {
  /**
   * @param {number} line
   * @param {string} type
   * @param {?} data
   */
  constructor(line, type, data) {
    this._line = line;
    this._type = type;
    this._data = data;
  }

  /**
   * @return {number}
   */
  line() {
    return this._line;
  }

  /**
   * @return {string}
   */
  type() {
    return this._type;
  }

  /**
   * @return {*}
   */
  data() {
    return this._data;
  }
};

/**
 * @unrestricted
 */
Workspace.UISourceCodeMetadata = class {
  /**
   * @param {?Date} modificationTime
   * @param {?number} contentSize
   */
  constructor(modificationTime, contentSize) {
    this.modificationTime = modificationTime;
    this.contentSize = contentSize;
  }
};
