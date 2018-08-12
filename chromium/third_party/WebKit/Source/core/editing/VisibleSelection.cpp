/*
 * Copyright (C) 2004, 2005, 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/VisibleSelection.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/Range.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/SelectionAdjuster.h"
#include "core/editing/iterators/CharacterIterator.h"
#include "core/layout/LayoutObject.h"
#include "platform/geometry/LayoutPoint.h"
#include "wtf/Assertions.h"
#include "wtf/text/CString.h"
#include "wtf/text/CharacterNames.h"
#include "wtf/text/StringBuilder.h"

namespace blink {

template <typename Strategy>
VisibleSelectionTemplate<Strategy>::VisibleSelectionTemplate()
    : m_affinity(TextAffinity::Downstream),
      m_selectionType(NoSelection),
      m_baseIsFirst(true),
      m_isDirectional(false),
      m_granularity(CharacterGranularity),
      m_hasTrailingWhitespace(false) {}

template <typename Strategy>
VisibleSelectionTemplate<Strategy>::VisibleSelectionTemplate(
    const PositionTemplate<Strategy>& base,
    const PositionTemplate<Strategy>& extent,
    TextAffinity affinity,
    bool isDirectional)
    : m_base(base),
      m_extent(extent),
      m_affinity(affinity),
      m_isDirectional(isDirectional),
      m_granularity(CharacterGranularity),
      m_hasTrailingWhitespace(false) {
  validate();
}

template <typename Strategy>
VisibleSelectionTemplate<Strategy> VisibleSelectionTemplate<Strategy>::create(
    const PositionTemplate<Strategy>& base,
    const PositionTemplate<Strategy>& extent,
    TextAffinity affinity,
    bool isDirectional) {
  return VisibleSelectionTemplate(base, extent, affinity, isDirectional);
}

VisibleSelection createVisibleSelectionDeprecated(const Position& pos,
                                                  TextAffinity affinity,
                                                  bool isDirectional) {
  if (pos.isNotNull())
    pos.document()->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelection::create(pos, pos, affinity, isDirectional);
}

VisibleSelection createVisibleSelectionDeprecated(const Position& base,
                                                  const Position& extent,
                                                  TextAffinity affinity,
                                                  bool isDirectional) {
  if (base.isNotNull())
    base.document()->updateStyleAndLayoutIgnorePendingStylesheets();
  if (extent.isNotNull())
    extent.document()->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelection::create(base, extent, affinity, isDirectional);
}

VisibleSelection createVisibleSelectionDeprecated(
    const PositionWithAffinity& pos,
    bool isDirectional) {
  if (pos.isNotNull())
    pos.position().document()->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelection::create(pos.position(), pos.position(),
                                  pos.affinity(), isDirectional);
}

VisibleSelection createVisibleSelectionDeprecated(const VisiblePosition& pos,
                                                  bool isDirectional) {
  if (pos.isNotNull())
    pos.deepEquivalent()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelection::create(pos.deepEquivalent(), pos.deepEquivalent(),
                                  pos.affinity(), isDirectional);
}

VisibleSelection createVisibleSelectionDeprecated(const VisiblePosition& base,
                                                  const VisiblePosition& extent,
                                                  bool isDirectional) {
  if (base.isNotNull())
    base.deepEquivalent()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  if (extent.isNotNull())
    extent.deepEquivalent()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelection::create(base.deepEquivalent(),
                                  extent.deepEquivalent(), base.affinity(),
                                  isDirectional);
}

VisibleSelection createVisibleSelectionDeprecated(const EphemeralRange& range,
                                                  TextAffinity affinity,
                                                  bool isDirectional) {
  if (range.isNotNull())
    range.startPosition()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelection::create(range.startPosition(), range.endPosition(),
                                  affinity, isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelectionDeprecated(
    const PositionInFlatTree& pos,
    TextAffinity affinity,
    bool isDirectional) {
  if (pos.isNotNull())
    pos.document()->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelectionInFlatTree::create(pos, pos, affinity, isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelectionDeprecated(
    const PositionInFlatTree& base,
    const PositionInFlatTree& extent,
    TextAffinity affinity,
    bool isDirectional) {
  if (base.isNotNull())
    base.document()->updateStyleAndLayoutIgnorePendingStylesheets();
  if (extent.isNotNull())
    extent.document()->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelectionInFlatTree::create(base, extent, affinity,
                                            isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelectionDeprecated(
    const PositionInFlatTreeWithAffinity& pos,
    bool isDirectional) {
  if (pos.isNotNull())
    pos.position().document()->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelectionInFlatTree::create(pos.position(), pos.position(),
                                            pos.affinity(), isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelectionDeprecated(
    const VisiblePositionInFlatTree& pos,
    bool isDirectional) {
  if (pos.isNotNull())
    pos.deepEquivalent()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelectionInFlatTree::create(pos.deepEquivalent(),
                                            pos.deepEquivalent(),
                                            pos.affinity(), isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelectionDeprecated(
    const VisiblePositionInFlatTree& base,
    const VisiblePositionInFlatTree& extent,
    bool isDirectional) {
  if (base.isNotNull())
    base.deepEquivalent()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  if (extent.isNotNull())
    extent.deepEquivalent()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelectionInFlatTree::create(base.deepEquivalent(),
                                            extent.deepEquivalent(),
                                            base.affinity(), isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelectionDeprecated(
    const EphemeralRangeInFlatTree& range,
    TextAffinity affinity,
    bool isDirectional) {
  if (range.isNotNull())
    range.startPosition()
        .document()
        ->updateStyleAndLayoutIgnorePendingStylesheets();
  return VisibleSelectionInFlatTree::create(
      range.startPosition(), range.endPosition(), affinity, isDirectional);
}

VisibleSelection createVisibleSelection(const Position& pos,
                                        TextAffinity affinity,
                                        bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(pos));
  return VisibleSelection::create(pos, pos, affinity, isDirectional);
}

VisibleSelection createVisibleSelection(const Position& base,
                                        const Position& extent,
                                        TextAffinity affinity,
                                        bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(base));
  DCHECK(!needsLayoutTreeUpdate(extent));
  // TODO(xiaochengh): We should check |base.isNotNull() || extent.isNull()|
  // after all call sites have ensured that.
  return VisibleSelection::create(base, extent, affinity, isDirectional);
}

VisibleSelection createVisibleSelection(const PositionWithAffinity& pos,
                                        bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(pos.position()));
  return VisibleSelection::create(pos.position(), pos.position(),
                                  pos.affinity(), isDirectional);
}

VisibleSelection createVisibleSelection(const VisiblePosition& pos,
                                        bool isDirectional) {
  DCHECK(pos.isValid());
  return VisibleSelection::create(pos.deepEquivalent(), pos.deepEquivalent(),
                                  pos.affinity(), isDirectional);
}

VisibleSelection createVisibleSelection(const VisiblePosition& base,
                                        const VisiblePosition& extent,
                                        bool isDirectional) {
  DCHECK(base.isValid());
  DCHECK(extent.isValid());
  // TODO(xiaochengh): We should check |base.isNotNull() || extent.isNull()|
  // after all call sites have ensured that.
  return VisibleSelection::create(base.deepEquivalent(),
                                  extent.deepEquivalent(), base.affinity(),
                                  isDirectional);
}

VisibleSelection createVisibleSelection(const EphemeralRange& range,
                                        TextAffinity affinity,
                                        bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(range.startPosition()));
  DCHECK(!needsLayoutTreeUpdate(range.endPosition()));
  return VisibleSelection::create(range.startPosition(), range.endPosition(),
                                  affinity, isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelection(const PositionInFlatTree& pos,
                                                  TextAffinity affinity,
                                                  bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(pos));
  return VisibleSelectionInFlatTree::create(pos, pos, affinity, isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelection(
    const PositionInFlatTree& base,
    const PositionInFlatTree& extent,
    TextAffinity affinity,
    bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(base));
  DCHECK(!needsLayoutTreeUpdate(extent));
  // TODO(xiaochengh): We should check |base.isNotNull() || extent.isNull()|
  // after all call sites have ensured that.
  return VisibleSelectionInFlatTree::create(base, extent, affinity,
                                            isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelection(
    const PositionInFlatTreeWithAffinity& pos,
    bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(pos.position()));
  return VisibleSelectionInFlatTree::create(pos.position(), pos.position(),
                                            pos.affinity(), isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelection(
    const VisiblePositionInFlatTree& pos,
    bool isDirectional) {
  DCHECK(pos.isValid());
  return VisibleSelectionInFlatTree::create(pos.deepEquivalent(),
                                            pos.deepEquivalent(),
                                            pos.affinity(), isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelection(
    const VisiblePositionInFlatTree& base,
    const VisiblePositionInFlatTree& extent,
    bool isDirectional) {
  DCHECK(base.isValid());
  DCHECK(extent.isValid());
  // TODO(xiaochengh): We should check |base.isNotNull() || extent.isNull()|
  // after all call sites have ensured that.
  return VisibleSelectionInFlatTree::create(base.deepEquivalent(),
                                            extent.deepEquivalent(),
                                            base.affinity(), isDirectional);
}

VisibleSelectionInFlatTree createVisibleSelection(
    const EphemeralRangeInFlatTree& range,
    TextAffinity affinity,
    bool isDirectional) {
  DCHECK(!needsLayoutTreeUpdate(range.startPosition()));
  DCHECK(!needsLayoutTreeUpdate(range.endPosition()));
  return VisibleSelectionInFlatTree::create(
      range.startPosition(), range.endPosition(), affinity, isDirectional);
}

template <typename Strategy>
static SelectionType computeSelectionType(
    const PositionTemplate<Strategy>& start,
    const PositionTemplate<Strategy>& end) {
  if (start.isNull()) {
    DCHECK(end.isNull());
    return NoSelection;
  }
  if (start == end)
    return CaretSelection;
  // TODO(yosin) We should call |Document::updateStyleAndLayout()| here for
  // |mostBackwardCaretPosition()|. However, we are here during
  // |Node::removeChild()|.
  start.anchorNode()->updateDistribution();
  end.anchorNode()->updateDistribution();
  if (mostBackwardCaretPosition(start) == mostBackwardCaretPosition(end))
    return CaretSelection;
  return RangeSelection;
}

template <typename Strategy>
VisibleSelectionTemplate<Strategy>::VisibleSelectionTemplate(
    const VisibleSelectionTemplate<Strategy>& other)
    : m_base(other.m_base),
      m_extent(other.m_extent),
      m_start(other.m_start),
      m_end(other.m_end),
      m_affinity(other.m_affinity),
      m_selectionType(other.m_selectionType),
      m_baseIsFirst(other.m_baseIsFirst),
      m_isDirectional(other.m_isDirectional),
      m_granularity(other.m_granularity),
      m_hasTrailingWhitespace(other.m_hasTrailingWhitespace) {}

template <typename Strategy>
VisibleSelectionTemplate<Strategy>& VisibleSelectionTemplate<Strategy>::
operator=(const VisibleSelectionTemplate<Strategy>& other) {
  m_base = other.m_base;
  m_extent = other.m_extent;
  m_start = other.m_start;
  m_end = other.m_end;
  m_affinity = other.m_affinity;
  m_selectionType = other.m_selectionType;
  m_baseIsFirst = other.m_baseIsFirst;
  m_isDirectional = other.m_isDirectional;
  m_granularity = other.m_granularity;
  m_hasTrailingWhitespace = other.m_hasTrailingWhitespace;
  return *this;
}

template <typename Strategy>
VisibleSelectionTemplate<Strategy>
VisibleSelectionTemplate<Strategy>::selectionFromContentsOfNode(Node* node) {
  DCHECK(!Strategy::editingIgnoresContent(node));

  // TODO(xiaochengh): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited. see http://crbug.com/590369 for more details.
  node->document().updateStyleAndLayoutIgnorePendingStylesheets();

  return VisibleSelectionTemplate::create(
      PositionTemplate<Strategy>::firstPositionInNode(node),
      PositionTemplate<Strategy>::lastPositionInNode(node), SelDefaultAffinity,
      false);
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setBase(
    const PositionTemplate<Strategy>& position) {
  DCHECK(!needsLayoutTreeUpdate(position));
  m_base = position;
  validate();
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setBase(
    const VisiblePositionTemplate<Strategy>& visiblePosition) {
  DCHECK(visiblePosition.isValid());
  m_base = visiblePosition.deepEquivalent();
  validate();
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setExtent(
    const PositionTemplate<Strategy>& position) {
  DCHECK(!needsLayoutTreeUpdate(position));
  m_extent = position;
  validate();
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setExtent(
    const VisiblePositionTemplate<Strategy>& visiblePosition) {
  DCHECK(visiblePosition.isValid());
  m_extent = visiblePosition.deepEquivalent();
  validate();
}

EphemeralRange firstEphemeralRangeOf(const VisibleSelection& selection) {
  if (selection.isNone())
    return EphemeralRange();
  Position start = selection.start().parentAnchoredEquivalent();
  Position end = selection.end().parentAnchoredEquivalent();
  return EphemeralRange(start, end);
}

Range* firstRangeOf(const VisibleSelection& selection) {
  return createRange(firstEphemeralRangeOf(selection));
}

template <typename Strategy>
EphemeralRangeTemplate<Strategy>
VisibleSelectionTemplate<Strategy>::toNormalizedEphemeralRange() const {
  if (isNone())
    return EphemeralRangeTemplate<Strategy>();

  // Make sure we have an updated layout since this function is called
  // in the course of running edit commands which modify the DOM.
  // Failing to call this can result in equivalentXXXPosition calls returning
  // incorrect results.
  m_start.document()->updateStyleAndLayout();

  // Check again, because updating layout can clear the selection.
  if (isNone())
    return EphemeralRangeTemplate<Strategy>();

  if (isCaret()) {
    // If the selection is a caret, move the range start upstream. This
    // helps us match the conventions of text editors tested, which make
    // style determinations based on the character before the caret, if any.
    const PositionTemplate<Strategy> start =
        mostBackwardCaretPosition(m_start).parentAnchoredEquivalent();
    return EphemeralRangeTemplate<Strategy>(start, start);
  }
  // If the selection is a range, select the minimum range that encompasses
  // the selection. Again, this is to match the conventions of text editors
  // tested, which make style determinations based on the first character of
  // the selection. For instance, this operation helps to make sure that the
  // "X" selected below is the only thing selected. The range should not be
  // allowed to "leak" out to the end of the previous text node, or to the
  // beginning of the next text node, each of which has a different style.
  //
  // On a treasure map, <b>X</b> marks the spot.
  //                       ^ selected
  //
  DCHECK(isRange());
  return normalizeRange(EphemeralRangeTemplate<Strategy>(m_start, m_end));
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::expandUsingGranularity(
    TextGranularity granularity) {
  if (isNone())
    return;
  validate(granularity);
}

template <typename Strategy>
static EphemeralRangeTemplate<Strategy> makeSearchRange(
    const PositionTemplate<Strategy>& pos) {
  Node* node = pos.anchorNode();
  if (!node)
    return EphemeralRangeTemplate<Strategy>();
  Document& document = node->document();
  if (!document.documentElement())
    return EphemeralRangeTemplate<Strategy>();
  Element* boundary = enclosingBlockFlowElement(*node);
  if (!boundary)
    return EphemeralRangeTemplate<Strategy>();

  return EphemeralRangeTemplate<Strategy>(
      pos, PositionTemplate<Strategy>::lastPositionInNode(boundary));
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::appendTrailingWhitespace() {
  DCHECK_EQ(m_granularity, WordGranularity);
  const EphemeralRangeTemplate<Strategy> searchRange = makeSearchRange(end());
  if (searchRange.isNull())
    return;

  CharacterIteratorAlgorithm<Strategy> charIt(
      searchRange.startPosition(), searchRange.endPosition(),
      TextIteratorEmitsCharactersBetweenAllVisiblePositions);
  bool changed = false;

  for (; charIt.length(); charIt.advance(1)) {
    UChar c = charIt.characterAt(0);
    if ((!isSpaceOrNewline(c) && c != noBreakSpaceCharacter) || c == '\n')
      break;
    m_end = charIt.endPosition();
    changed = true;
  }
  if (!changed)
    return;
  m_hasTrailingWhitespace = true;
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setBaseAndExtentToDeepEquivalents() {
  // Move the selection to rendered positions, if possible.
  bool baseAndExtentEqual = m_base == m_extent;
  if (m_base.isNotNull()) {
    m_base = createVisiblePosition(m_base, m_affinity).deepEquivalent();
    if (baseAndExtentEqual)
      m_extent = m_base;
  }
  if (m_extent.isNotNull() && !baseAndExtentEqual)
    m_extent = createVisiblePosition(m_extent, m_affinity).deepEquivalent();

  // Make sure we do not have a dangling base or extent.
  if (m_base.isNull() && m_extent.isNull()) {
    m_baseIsFirst = true;
  } else if (m_base.isNull()) {
    m_base = m_extent;
    m_baseIsFirst = true;
  } else if (m_extent.isNull()) {
    m_extent = m_base;
    m_baseIsFirst = true;
  } else {
    m_baseIsFirst = m_base.compareTo(m_extent) <= 0;
  }
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setStartRespectingGranularity(
    TextGranularity granularity) {
  DCHECK(m_base.isNotNull());
  DCHECK(m_extent.isNotNull());

  m_start = m_baseIsFirst ? m_base : m_extent;

  switch (granularity) {
    case CharacterGranularity:
      // Don't do any expansion.
      break;
    case WordGranularity: {
      // General case: Select the word the caret is positioned inside of.
      // If the caret is on the word boundary, select the word according to
      // |wordSide|.
      // Edge case: If the caret is after the last word in a soft-wrapped line
      // or the last word in the document, select that last word
      // (LeftWordIfOnBoundary).
      // Edge case: If the caret is after the last word in a paragraph, select
      // from the the end of the last word to the line break (also
      // RightWordIfOnBoundary);
      const VisiblePositionTemplate<Strategy> visibleStart =
          createVisiblePosition(m_start, m_affinity);
      EWordSide side = RightWordIfOnBoundary;
      if (isEndOfEditableOrNonEditableContent(visibleStart) ||
          (isEndOfLine(visibleStart) && !isStartOfLine(visibleStart) &&
           !isEndOfParagraph(visibleStart)))
        side = LeftWordIfOnBoundary;
      m_start = startOfWord(visibleStart, side).deepEquivalent();
      break;
    }
    case SentenceGranularity: {
      m_start = startOfSentence(createVisiblePosition(m_start, m_affinity))
                    .deepEquivalent();
      break;
    }
    case LineGranularity: {
      m_start = startOfLine(createVisiblePosition(m_start, m_affinity))
                    .deepEquivalent();
      break;
    }
    case LineBoundary:
      m_start = startOfLine(createVisiblePosition(m_start, m_affinity))
                    .deepEquivalent();
      break;
    case ParagraphGranularity: {
      VisiblePositionTemplate<Strategy> pos =
          createVisiblePosition(m_start, m_affinity);
      if (isStartOfLine(pos) && isEndOfEditableOrNonEditableContent(pos))
        pos = previousPositionOf(pos);
      m_start = startOfParagraph(pos).deepEquivalent();
      break;
    }
    case DocumentBoundary:
      m_start = startOfDocument(createVisiblePosition(m_start, m_affinity))
                    .deepEquivalent();
      break;
    case ParagraphBoundary:
      m_start = startOfParagraph(createVisiblePosition(m_start, m_affinity))
                    .deepEquivalent();
      break;
    case SentenceBoundary:
      m_start = startOfSentence(createVisiblePosition(m_start, m_affinity))
                    .deepEquivalent();
      break;
  }

  // Make sure we do not have a Null position.
  if (m_start.isNull())
    m_start = m_baseIsFirst ? m_base : m_extent;
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setEndRespectingGranularity(
    TextGranularity granularity) {
  DCHECK(m_base.isNotNull());
  DCHECK(m_extent.isNotNull());

  m_end = m_baseIsFirst ? m_extent : m_base;

  switch (granularity) {
    case CharacterGranularity:
      // Don't do any expansion.
      break;
    case WordGranularity: {
      // General case: Select the word the caret is positioned inside of.
      // If the caret is on the word boundary, select the word according to
      // |wordSide|.
      // Edge case: If the caret is after the last word in a soft-wrapped line
      // or the last word in the document, select that last word
      // (|LeftWordIfOnBoundary|).
      // Edge case: If the caret is after the last word in a paragraph, select
      // from the the end of the last word to the line break (also
      // |RightWordIfOnBoundary|);
      const VisiblePositionTemplate<Strategy> originalEnd =
          createVisiblePosition(m_end, m_affinity);
      EWordSide side = RightWordIfOnBoundary;
      if (isEndOfEditableOrNonEditableContent(originalEnd) ||
          (isEndOfLine(originalEnd) && !isStartOfLine(originalEnd) &&
           !isEndOfParagraph(originalEnd)))
        side = LeftWordIfOnBoundary;

      const VisiblePositionTemplate<Strategy> wordEnd =
          endOfWord(originalEnd, side);
      VisiblePositionTemplate<Strategy> end = wordEnd;

      if (isEndOfParagraph(originalEnd) &&
          !isEmptyTableCell(m_start.anchorNode())) {
        // Select the paragraph break (the space from the end of a paragraph
        // to the start of the next one) to match TextEdit.
        end = nextPositionOf(wordEnd);

        if (Element* table = tableElementJustBefore(end)) {
          // The paragraph break after the last paragraph in the last cell
          // of a block table ends at the start of the paragraph after the
          // table.
          if (isEnclosingBlock(table))
            end = nextPositionOf(end, CannotCrossEditingBoundary);
          else
            end = wordEnd;
        }

        if (end.isNull())
          end = wordEnd;
      }

      m_end = end.deepEquivalent();
      break;
    }
    case SentenceGranularity: {
      m_end = endOfSentence(createVisiblePosition(m_end, m_affinity))
                  .deepEquivalent();
      break;
    }
    case LineGranularity: {
      VisiblePositionTemplate<Strategy> end =
          endOfLine(createVisiblePosition(m_end, m_affinity));
      // If the end of this line is at the end of a paragraph, include the
      // space after the end of the line in the selection.
      if (isEndOfParagraph(end)) {
        VisiblePositionTemplate<Strategy> next = nextPositionOf(end);
        if (next.isNotNull())
          end = next;
      }
      m_end = end.deepEquivalent();
      break;
    }
    case LineBoundary:
      m_end =
          endOfLine(createVisiblePosition(m_end, m_affinity)).deepEquivalent();
      break;
    case ParagraphGranularity: {
      const VisiblePositionTemplate<Strategy> visibleParagraphEnd =
          endOfParagraph(createVisiblePosition(m_end, m_affinity));

      // Include the "paragraph break" (the space from the end of this
      // paragraph to the start of the next one) in the selection.
      VisiblePositionTemplate<Strategy> end =
          nextPositionOf(visibleParagraphEnd);

      if (Element* table = tableElementJustBefore(end)) {
        // The paragraph break after the last paragraph in the last cell of
        // a block table ends at the start of the paragraph after the table,
        // not at the position just after the table.
        if (isEnclosingBlock(table)) {
          end = nextPositionOf(end, CannotCrossEditingBoundary);
        } else {
          // There is no parargraph break after the last paragraph in the
          // last cell of an inline table.
          end = visibleParagraphEnd;
        }
      }

      if (end.isNull())
        end = visibleParagraphEnd;

      m_end = end.deepEquivalent();
      break;
    }
    case DocumentBoundary:
      m_end = endOfDocument(createVisiblePosition(m_end, m_affinity))
                  .deepEquivalent();
      break;
    case ParagraphBoundary:
      m_end = endOfParagraph(createVisiblePosition(m_end, m_affinity))
                  .deepEquivalent();
      break;
    case SentenceBoundary:
      m_end = endOfSentence(createVisiblePosition(m_end, m_affinity))
                  .deepEquivalent();
      break;
  }

  // Make sure we do not have a Null position.
  if (m_end.isNull())
    m_end = m_baseIsFirst ? m_extent : m_base;
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::updateSelectionType() {
  m_selectionType = computeSelectionType(m_start, m_end);

  // Affinity only makes sense for a caret
  if (m_selectionType != CaretSelection)
    m_affinity = TextAffinity::Downstream;
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::validate(TextGranularity granularity) {
  DCHECK(!needsLayoutTreeUpdate(m_base));
  DCHECK(!needsLayoutTreeUpdate(m_extent));
  // TODO(xiaochengh): Add a DocumentLifecycle::DisallowTransitionScope here.

  m_granularity = granularity;
  m_hasTrailingWhitespace = false;
  setBaseAndExtentToDeepEquivalents();
  if (m_base.isNull() || m_extent.isNull()) {
    m_base = m_extent = m_start = m_end = PositionTemplate<Strategy>();
    updateSelectionType();
    return;
  }

  m_start = m_baseIsFirst ? m_base : m_extent;
  m_end = m_baseIsFirst ? m_extent : m_base;
  setStartRespectingGranularity(granularity);
  DCHECK(m_start.isNotNull());
  setEndRespectingGranularity(granularity);
  DCHECK(m_end.isNotNull());
  adjustSelectionToAvoidCrossingShadowBoundaries();
  adjustSelectionToAvoidCrossingEditingBoundaries();
  updateSelectionType();

  if (getSelectionType() == RangeSelection) {
    // "Constrain" the selection to be the smallest equivalent range of
    // nodes. This is a somewhat arbitrary choice, but experience shows that
    // it is useful to make to make the selection "canonical" (if only for
    // purposes of comparing selections). This is an ideal point of the code
    // to do this operation, since all selection changes that result in a
    // RANGE come through here before anyone uses it.
    // TODO(yosin) Canonicalizing is good, but haven't we already done it
    // (when we set these two positions to |VisiblePosition|
    // |deepEquivalent()|s above)?
    m_start = mostForwardCaretPosition(m_start);
    m_end = mostBackwardCaretPosition(m_end);
  }
}

template <typename Strategy>
bool VisibleSelectionTemplate<Strategy>::isValidFor(
    const Document& document) const {
  if (isNone())
    return true;

  return m_base.document() == &document && !m_base.isOrphan() &&
         !m_extent.isOrphan() && !m_start.isOrphan() && !m_end.isOrphan();
}

// TODO(yosin) This function breaks the invariant of this class.
// But because we use VisibleSelection to store values in editing commands for
// use when undoing the command, we need to be able to create a selection that
// while currently invalid, will be valid once the changes are undone. This is a
// design problem. To fix it we either need to change the invariants of
// |VisibleSelection| or create a new class for editing to use that can
// manipulate selections that are not currently valid.
template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::setWithoutValidation(
    const PositionTemplate<Strategy>& base,
    const PositionTemplate<Strategy>& extent) {
  if (base.isNull() || extent.isNull()) {
    m_base = m_extent = m_start = m_end = PositionTemplate<Strategy>();
    updateSelectionType();
    return;
  }

  m_base = base;
  m_extent = extent;
  m_baseIsFirst = base.compareTo(extent) <= 0;
  if (m_baseIsFirst) {
    m_start = base;
    m_end = extent;
  } else {
    m_start = extent;
    m_end = base;
  }
  m_selectionType = base == extent ? CaretSelection : RangeSelection;
  if (m_selectionType != CaretSelection) {
    // Since |m_affinity| for non-|CaretSelection| is always |Downstream|,
    // we should keep this invariant. Note: This function can be called with
    // |m_affinity| is |TextAffinity::Upstream|.
    m_affinity = TextAffinity::Downstream;
  }
}

template <typename Strategy>
void VisibleSelectionTemplate<
    Strategy>::adjustSelectionToAvoidCrossingShadowBoundaries() {
  if (m_base.isNull() || m_start.isNull() || m_base.isNull())
    return;
  SelectionAdjuster::adjustSelectionToAvoidCrossingShadowBoundaries(this);
}

static Element* lowestEditableAncestor(Node* node) {
  while (node) {
    if (hasEditableStyle(*node))
      return rootEditableElement(*node);
    if (isHTMLBodyElement(*node))
      break;
    node = node->parentNode();
  }

  return nullptr;
}

template <typename Strategy>
void VisibleSelectionTemplate<
    Strategy>::adjustSelectionToAvoidCrossingEditingBoundaries() {
  if (m_base.isNull() || m_start.isNull() || m_end.isNull())
    return;

  ContainerNode* baseRoot = highestEditableRoot(m_base);
  ContainerNode* startRoot = highestEditableRoot(m_start);
  ContainerNode* endRoot = highestEditableRoot(m_end);

  Element* baseEditableAncestor =
      lowestEditableAncestor(m_base.computeContainerNode());

  // The base, start and end are all in the same region.  No adjustment
  // necessary.
  if (baseRoot == startRoot && baseRoot == endRoot)
    return;

  // The selection is based in editable content.
  if (baseRoot) {
    // If the start is outside the base's editable root, cap it at the start of
    // that root.
    // If the start is in non-editable content that is inside the base's
    // editable root, put it at the first editable position after start inside
    // the base's editable root.
    if (startRoot != baseRoot) {
      const VisiblePositionTemplate<Strategy> first =
          firstEditableVisiblePositionAfterPositionInRoot(m_start, *baseRoot);
      m_start = first.deepEquivalent();
      if (m_start.isNull()) {
        NOTREACHED();
        m_start = m_end;
      }
    }
    // If the end is outside the base's editable root, cap it at the end of that
    // root.
    // If the end is in non-editable content that is inside the base's root, put
    // it at the last editable position before the end inside the base's root.
    if (endRoot != baseRoot) {
      const VisiblePositionTemplate<Strategy> last =
          lastEditableVisiblePositionBeforePositionInRoot(m_end, *baseRoot);
      m_end = last.deepEquivalent();
      if (m_end.isNull())
        m_end = m_start;
    }
    // The selection is based in non-editable content.
  } else {
    // FIXME: Non-editable pieces inside editable content should be atomic, in
    // the same way that editable pieces in non-editable content are atomic.

    // The selection ends in editable content or non-editable content inside a
    // different editable ancestor, move backward until non-editable content
    // inside the same lowest editable ancestor is reached.
    Element* endEditableAncestor =
        lowestEditableAncestor(m_end.computeContainerNode());
    if (endRoot || endEditableAncestor != baseEditableAncestor) {
      PositionTemplate<Strategy> p = previousVisuallyDistinctCandidate(m_end);
      Element* shadowAncestor = endRoot ? endRoot->ownerShadowHost() : nullptr;
      if (p.isNull() && shadowAncestor)
        p = PositionTemplate<Strategy>::afterNode(shadowAncestor);
      while (p.isNotNull() &&
             !(lowestEditableAncestor(p.computeContainerNode()) ==
                   baseEditableAncestor &&
               !isEditablePosition(p))) {
        Element* root = rootEditableElementOf(p);
        shadowAncestor = root ? root->ownerShadowHost() : nullptr;
        p = isAtomicNode(p.computeContainerNode())
                ? PositionTemplate<Strategy>::inParentBeforeNode(
                      *p.computeContainerNode())
                : previousVisuallyDistinctCandidate(p);
        if (p.isNull() && shadowAncestor)
          p = PositionTemplate<Strategy>::afterNode(shadowAncestor);
      }
      const VisiblePositionTemplate<Strategy> previous =
          createVisiblePosition(p);

      if (previous.isNull()) {
        // The selection crosses an Editing boundary.  This is a
        // programmer error in the editing code.  Happy debugging!
        NOTREACHED();
        m_base = PositionTemplate<Strategy>();
        m_extent = PositionTemplate<Strategy>();
        validate();
        return;
      }
      m_end = previous.deepEquivalent();
    }

    // The selection starts in editable content or non-editable content inside a
    // different editable ancestor, move forward until non-editable content
    // inside the same lowest editable ancestor is reached.
    Element* startEditableAncestor =
        lowestEditableAncestor(m_start.computeContainerNode());
    if (startRoot || startEditableAncestor != baseEditableAncestor) {
      PositionTemplate<Strategy> p = nextVisuallyDistinctCandidate(m_start);
      Element* shadowAncestor =
          startRoot ? startRoot->ownerShadowHost() : nullptr;
      if (p.isNull() && shadowAncestor)
        p = PositionTemplate<Strategy>::beforeNode(shadowAncestor);
      while (p.isNotNull() &&
             !(lowestEditableAncestor(p.computeContainerNode()) ==
                   baseEditableAncestor &&
               !isEditablePosition(p))) {
        Element* root = rootEditableElementOf(p);
        shadowAncestor = root ? root->ownerShadowHost() : nullptr;
        p = isAtomicNode(p.computeContainerNode())
                ? PositionTemplate<Strategy>::inParentAfterNode(
                      *p.computeContainerNode())
                : nextVisuallyDistinctCandidate(p);
        if (p.isNull() && shadowAncestor)
          p = PositionTemplate<Strategy>::beforeNode(shadowAncestor);
      }
      const VisiblePositionTemplate<Strategy> next = createVisiblePosition(p);

      if (next.isNull()) {
        // The selection crosses an Editing boundary.  This is a
        // programmer error in the editing code.  Happy debugging!
        NOTREACHED();
        m_base = PositionTemplate<Strategy>();
        m_extent = PositionTemplate<Strategy>();
        validate();
        return;
      }
      m_start = next.deepEquivalent();
    }
  }

  // Correct the extent if necessary.
  if (baseEditableAncestor !=
      lowestEditableAncestor(m_extent.computeContainerNode()))
    m_extent = m_baseIsFirst ? m_end : m_start;
}

template <typename Strategy>
bool VisibleSelectionTemplate<Strategy>::isContentEditable() const {
  return isEditablePosition(start());
}

template <typename Strategy>
bool VisibleSelectionTemplate<Strategy>::hasEditableStyle() const {
  return isEditablePosition(start());
}

template <typename Strategy>
bool VisibleSelectionTemplate<Strategy>::isContentRichlyEditable() const {
  return isRichlyEditablePosition(toPositionInDOMTree(start()));
}

template <typename Strategy>
Element* VisibleSelectionTemplate<Strategy>::rootEditableElement() const {
  return rootEditableElementOf(start());
}

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::updateIfNeeded() {
  Document* document = m_base.document();
  if (!document)
    return;
  document->updateStyleAndLayoutIgnorePendingStylesheets();
  const bool hasTrailingWhitespace = m_hasTrailingWhitespace;
  validate(m_granularity);
  if (!hasTrailingWhitespace)
    return;
  appendTrailingWhitespace();
}

// TODO(yosin): Since |validatePositionsIfNeeded()| is called just one place,
// we should move it to the call site.
template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::validatePositionsIfNeeded() {
  if (!m_base.isConnected() || !m_extent.isConnected()) {
    *this = VisibleSelectionTemplate();
    return;
  }
  updateIfNeeded();
}

template <typename Strategy>
static bool equalSelectionsAlgorithm(
    const VisibleSelectionTemplate<Strategy>& selection1,
    const VisibleSelectionTemplate<Strategy>& selection2) {
  if (selection1.affinity() != selection2.affinity() ||
      selection1.isDirectional() != selection2.isDirectional())
    return false;

  if (selection1.isNone())
    return selection2.isNone();

  const VisibleSelectionTemplate<Strategy> selectionWrapper1(selection1);
  const VisibleSelectionTemplate<Strategy> selectionWrapper2(selection2);

  return selectionWrapper1.start() == selectionWrapper2.start() &&
         selectionWrapper1.end() == selectionWrapper2.end() &&
         selectionWrapper1.base() == selectionWrapper2.base() &&
         selectionWrapper1.extent() == selectionWrapper2.extent();
}

template <typename Strategy>
bool VisibleSelectionTemplate<Strategy>::operator==(
    const VisibleSelectionTemplate<Strategy>& other) const {
  return equalSelectionsAlgorithm<Strategy>(*this, other);
}

#ifndef NDEBUG

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::showTreeForThis() const {
  if (!start().anchorNode())
    return;
  LOG(INFO) << "\n"
            << start()
                   .anchorNode()
                   ->toMarkedTreeString(start().anchorNode(), "S",
                                        end().anchorNode(), "E")
                   .utf8()
                   .data()
            << "start: " << start().toAnchorTypeAndOffsetString().utf8().data()
            << "\n"
            << "end: " << end().toAnchorTypeAndOffsetString().utf8().data();
}

#endif

template <typename Strategy>
void VisibleSelectionTemplate<Strategy>::PrintTo(
    const VisibleSelectionTemplate<Strategy>& selection,
    std::ostream* ostream) {
  if (selection.isNone()) {
    *ostream << "VisibleSelection()";
    return;
  }
  *ostream << "VisibleSelection(base: " << selection.base()
           << " extent:" << selection.extent()
           << " start: " << selection.start() << " end: " << selection.end()
           << ' ' << selection.affinity() << ' '
           << (selection.isDirectional() ? "Directional" : "NonDirectional")
           << ')';
}

template class CORE_TEMPLATE_EXPORT VisibleSelectionTemplate<EditingStrategy>;
template class CORE_TEMPLATE_EXPORT
    VisibleSelectionTemplate<EditingInFlatTreeStrategy>;

std::ostream& operator<<(std::ostream& ostream,
                         const VisibleSelection& selection) {
  VisibleSelection::PrintTo(selection, &ostream);
  return ostream;
}

std::ostream& operator<<(std::ostream& ostream,
                         const VisibleSelectionInFlatTree& selection) {
  VisibleSelectionInFlatTree::PrintTo(selection, &ostream);
  return ostream;
}

}  // namespace blink

#ifndef NDEBUG

void showTree(const blink::VisibleSelection& sel) {
  sel.showTreeForThis();
}

void showTree(const blink::VisibleSelection* sel) {
  if (sel)
    sel->showTreeForThis();
}

void showTree(const blink::VisibleSelectionInFlatTree& sel) {
  sel.showTreeForThis();
}

void showTree(const blink::VisibleSelectionInFlatTree* sel) {
  if (sel)
    sel->showTreeForThis();
}
#endif
