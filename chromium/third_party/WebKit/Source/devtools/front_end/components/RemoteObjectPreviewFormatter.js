// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/**
 * @unrestricted
 */
Components.RemoteObjectPreviewFormatter = class {
  /**
   * @param {!Element} parentElement
   * @param {!Protocol.Runtime.ObjectPreview} preview
   */
  appendObjectPreview(parentElement, preview) {
    var description = preview.description;
    if (preview.type !== 'object' || preview.subtype === 'null') {
      parentElement.appendChild(this.renderPropertyPreview(preview.type, preview.subtype, description));
      return;
    }
    if (description && preview.subtype !== 'array' && preview.subtype !== 'typedarray') {
      var text = preview.subtype ? description : this._abbreviateFullQualifiedClassName(description);
      parentElement.createTextChildren(text, ' ');
    }
    if (preview.entries)
      this._appendEntriesPreview(parentElement, preview);
    else
      this._appendPropertiesPreview(parentElement, preview);
  }

  /**
   * @param {string} description
   * @return {string}
   */
  _abbreviateFullQualifiedClassName(description) {
    var abbreviatedDescription = description.split('.');
    for (var i = 0; i < abbreviatedDescription.length - 1; ++i)
      abbreviatedDescription[i] = abbreviatedDescription[i].trimMiddle(3);
    return abbreviatedDescription.join('.');
  }

  /**
   * @param {!Element} parentElement
   * @param {!Protocol.Runtime.ObjectPreview} preview
   */
  _appendPropertiesPreview(parentElement, preview) {
    var isArray = preview.subtype === 'array' || preview.subtype === 'typedarray';
    var arrayLength = SDK.RemoteObject.arrayLength(preview);
    var properties = preview.properties;
    if (isArray)
      properties = properties.slice().stableSort(compareIndexesFirst);
    else
      properties = properties.slice().stableSort(compareFunctionsLast);

    /**
     * @param {!Protocol.Runtime.PropertyPreview} a
     * @param {!Protocol.Runtime.PropertyPreview} b
     */
    function compareFunctionsLast(a, b) {
      if (a.type !== 'function' && b.type === 'function')
        return -1;
      if (a.type === 'function' && b.type !== 'function')
        return 1;
      return 0;
    }

    /**
     * @param {!Protocol.Runtime.PropertyPreview} a
     * @param {!Protocol.Runtime.PropertyPreview} b
     */
    function compareIndexesFirst(a, b) {
      var index1 = toArrayIndex(a.name);
      var index2 = toArrayIndex(b.name);
      if (index1 < 0)
        return index2 < 0 ? 0 : 1;
      return index2 < 0 ? -1 : index1 - index2;
    }

    /**
     * @param {string} name
     * @return {number}
     */
    function toArrayIndex(name) {
      var index = name >>> 0;
      if (String(index) === name && index < arrayLength)
        return index;
      return -1;
    }

    parentElement.createTextChild(isArray ? '[' : '{');
    for (var i = 0; i < properties.length; ++i) {
      if (i > 0)
        parentElement.createTextChild(', ');

      var property = properties[i];
      var name = property.name;
      if (!isArray || name !== String(i) || i >= arrayLength) {
        if (/^\s|\s$|^$|\n/.test(name))
          parentElement.createChild('span', 'name').createTextChildren('"', name.replace(/\n/g, '\u21B5'), '"');
        else
          parentElement.createChild('span', 'name').textContent = name;
        parentElement.createTextChild(': ');
      }

      parentElement.appendChild(this._renderPropertyPreviewOrAccessor([property]));
    }
    if (preview.overflow)
      parentElement.createChild('span').textContent = '\u2026';
    parentElement.createTextChild(isArray ? ']' : '}');
  }

  /**
   * @param {!Element} parentElement
   * @param {!Protocol.Runtime.ObjectPreview} preview
   */
  _appendEntriesPreview(parentElement, preview) {
    parentElement.createTextChild('{');
    for (var i = 0; i < preview.entries.length; ++i) {
      if (i > 0)
        parentElement.createTextChild(', ');

      var entry = preview.entries[i];
      if (entry.key) {
        this.appendObjectPreview(parentElement, entry.key);
        parentElement.createTextChild(' => ');
      }
      this.appendObjectPreview(parentElement, entry.value);
    }
    if (preview.overflow)
      parentElement.createChild('span').textContent = '\u2026';
    parentElement.createTextChild('}');
  }

  /**
   * @param {!Array.<!Protocol.Runtime.PropertyPreview>} propertyPath
   * @return {!Element}
   */
  _renderPropertyPreviewOrAccessor(propertyPath) {
    var property = propertyPath.peekLast();
    return this.renderPropertyPreview(property.type, /** @type {string} */ (property.subtype), property.value);
  }

  /**
   * @param {string} type
   * @param {string=} subtype
   * @param {string=} description
   * @return {!Element}
   */
  renderPropertyPreview(type, subtype, description) {
    var span = createElementWithClass('span', 'object-value-' + (subtype || type));
    description = description || '';

    if (type === 'function') {
      span.textContent = 'function';
      return span;
    }

    if (type === 'object' && subtype === 'node' && description) {
      Components.DOMPresentationUtils.createSpansForNodeTitle(span, description);
      return span;
    }

    if (type === 'string') {
      span.createTextChildren('"', description.replace(/\n/g, '\u21B5'), '"');
      return span;
    }

    if (type === 'object' && !subtype) {
      span.textContent = this._abbreviateFullQualifiedClassName(description);
      return span;
    }

    span.textContent = description;
    return span;
  }
};
