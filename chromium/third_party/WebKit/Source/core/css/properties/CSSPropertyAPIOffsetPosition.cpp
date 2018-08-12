// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIOffsetPosition.h"

#include "core/css/CSSValuePair.h"
#include "core/css/parser/CSSParserContext.h"
#include "core/css/parser/CSSPropertyParserHelpers.h"
#include "core/frame/UseCounter.h"

namespace blink {

const CSSValue* CSSPropertyAPIOffsetPosition::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext& context,
    CSSPropertyID) {
  CSSValueID id = range.Peek().Id();
  if (id == CSSValueAuto)
    return CSSPropertyParserHelpers::ConsumeIdent(range);
  CSSValue* value = CSSPropertyParserHelpers::ConsumePosition(
      range, context.Mode(), CSSPropertyParserHelpers::UnitlessQuirk::kForbid);

  // Count when we receive a valid position other than 'auto'.
  if (value && value->IsValuePair())
    context.Count(UseCounter::kCSSOffsetInEffect);
  return value;
}

}  // namespace blink
