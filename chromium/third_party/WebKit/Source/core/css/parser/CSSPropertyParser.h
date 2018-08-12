/*
 * Copyright (C) 2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 - 2010  Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CSSPropertyParser_h
#define CSSPropertyParser_h

#include "core/css/CSSGridTemplateAreasValue.h"
#include "core/css/StyleRule.h"
#include "core/css/parser/CSSParserTokenRange.h"
#include "platform/Length.h"

namespace blink {

class CSSCustomIdentValue;
class CSSFunctionValue;
class CSSGradientValue;
class CSSGridLineNamesValue;
struct CSSParserString;
struct CSSParserValue;
class CSSParserValueList;
class CSSPrimitiveValue;
class CSSProperty;
class CSSValue;
class CSSValueList;
class StylePropertyShorthand;

// TODO(rob.buis) to move to cpp file once legacy parser is removed.
enum TrackSizeRestriction { FixedSizeOnly, AllowAll };

// Inputs: PropertyID, isImportant bool, CSSParserValueList.
// Outputs: Vector of CSSProperties

class CSSPropertyParser {
    STACK_ALLOCATED();
public:

    enum Units {
        FUnknown = 0x0000,
        FInteger = 0x0001,
        FNumber = 0x0002, // Real Numbers
        FPercent = 0x0004,
        FLength = 0x0008,
        FAngle = 0x0010,
        FTime = 0x0020,
        FFrequency = 0x0040,
        FPositiveInteger = 0x0080,
        FRelative = 0x0100,
        FResolution = 0x0200,
        FNonNeg = 0x0400,
        FUnitlessQuirk = 0x0800
    };

    static bool parseValue(CSSPropertyID, bool important,
        const CSSParserTokenRange&, const CSSParserContext&,
        HeapVector<CSSProperty, 256>&, StyleRule::RuleType);

    // Parses a non-shorthand CSS property
    static CSSValue* parseSingleValue(CSSPropertyID, const CSSParserTokenRange&, const CSSParserContext&);

    // TODO(timloh): This doesn't seem like the right place for these
    static bool isSystemColor(CSSValueID);
    static bool isColorKeyword(CSSValueID);
    static bool isValidNumericValue(double);

private:
    CSSPropertyParser(const CSSParserTokenRange&, const CSSParserContext&,
        HeapVector<CSSProperty, 256>*);

    // TODO(timloh): Rename once the CSSParserValue-based parseValue is removed
    bool parseValueStart(CSSPropertyID unresolvedProperty, bool important);
    bool consumeCSSWideKeyword(CSSPropertyID unresolvedProperty, bool important);
    CSSValue* parseSingleValue(CSSPropertyID);

    CSSValue* legacyParseValue(CSSPropertyID);
    bool legacyParseAndApplyValue(CSSPropertyID, bool important);
    bool legacyParseShorthand(CSSPropertyID, bool important);

    bool inShorthand() const { return m_inParseShorthand; }
    bool inQuirksMode() const { return isQuirksModeBehavior(m_context.mode()); }

    bool parseViewportDescriptor(CSSPropertyID propId, bool important);
    bool parseFontFaceDescriptor(CSSPropertyID);

    void addProperty(CSSPropertyID, CSSValue*, bool important, bool implicit = false);
    void addExpandedPropertyForValue(CSSPropertyID propId, CSSValue*, bool);

    bool consumeBorder(bool important);

    bool parseShorthand(CSSPropertyID, bool important);
    bool consumeShorthandGreedily(const StylePropertyShorthand&, bool important);
    bool consume4Values(const StylePropertyShorthand&, bool important);

    // Legacy parsing allows <string>s for animation-name
    bool consumeAnimationShorthand(const StylePropertyShorthand&, bool useLegacyParsing, bool important);
    bool consumeBackgroundShorthand(const StylePropertyShorthand&, bool important);

    bool consumeColumns(bool important);

    bool consumeGridItemPositionShorthand(CSSPropertyID, bool important);
    CSSValue* parseGridTemplateColumns(bool important);
    bool parseGridTemplateRowsAndAreasAndColumns(bool important);
    bool parseGridTemplateShorthand(bool important);
    bool parseGridShorthand(bool important);
    bool consumeGridAreaShorthand(bool important);
    CSSValue* parseGridTrackList();
    bool parseGridTrackRepeatFunction(CSSValueList&, bool& isAutoRepeat);
    CSSValue* parseGridTrackSize(CSSParserValueList& inputList, TrackSizeRestriction = AllowAll);
    CSSPrimitiveValue* parseGridBreadth(CSSParserValue*, TrackSizeRestriction = AllowAll);
    bool parseGridLineNames(CSSParserValueList&, CSSValueList&, CSSGridLineNamesValue* = nullptr);
    CSSValue* parseGridAutoFlow(CSSParserValueList&);

    bool consumeFont(bool important);
    bool consumeSystemFont(bool important);

    bool consumeBorderSpacing(bool important);

    // CSS3 Parsing Routines (for properties specific to CSS3)
    bool consumeBorderImage(CSSPropertyID, bool important);

    bool consumeFlex(bool important);

    bool consumeLegacyBreakProperty(CSSPropertyID, bool important);

    bool parseCalculation(CSSParserValue*, ValueRange);

    CSSPrimitiveValue* createPrimitiveNumericValue(CSSParserValue*);
    CSSCustomIdentValue* createPrimitiveCustomIdentValue(CSSParserValue*);

    class ShorthandScope {
        STACK_ALLOCATED();
    public:
        ShorthandScope(CSSPropertyParser* parser, CSSPropertyID propId) : m_parser(parser)
        {
            if (!(m_parser->m_inParseShorthand++))
                m_parser->m_currentShorthand = propId;
        }
        ~ShorthandScope()
        {
            if (!(--m_parser->m_inParseShorthand))
                m_parser->m_currentShorthand = CSSPropertyInvalid;
        }

    private:
        CSSPropertyParser* m_parser;
    };

    enum ReleaseParsedCalcValueCondition {
        ReleaseParsedCalcValue,
        DoNotReleaseParsedCalcValue
    };

    friend inline Units operator|(Units a, Units b)
    {
        return static_cast<Units>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    bool validCalculationUnit(CSSParserValue*, Units, ReleaseParsedCalcValueCondition releaseCalc = DoNotReleaseParsedCalcValue);

    bool shouldAcceptUnitLessValues(CSSParserValue*, Units, CSSParserMode);

    inline bool validUnit(CSSParserValue* value, Units unitflags, ReleaseParsedCalcValueCondition releaseCalc = DoNotReleaseParsedCalcValue) { return validUnit(value, unitflags, m_context.mode(), releaseCalc); }
    bool validUnit(CSSParserValue*, Units, CSSParserMode, ReleaseParsedCalcValueCondition releaseCalc = DoNotReleaseParsedCalcValue);

private:
    // Inputs:
    CSSParserValueList* m_valueList;
    CSSParserTokenRange m_range;
    const CSSParserContext& m_context;

    // Outputs:
    HeapVector<CSSProperty, 256>* m_parsedProperties;

    // Locals during parsing:
    int m_inParseShorthand;
    CSSPropertyID m_currentShorthand;
    Member<CSSCalcValue> m_parsedCalculation;
};

// TODO(rob.buis): should move to CSSPropertyParser after conversion.
bool allTracksAreFixedSized(CSSValueList&);
bool parseGridTemplateAreasRow(const String&, NamedGridAreaMap&, const size_t, size_t&);

CSSPropertyID unresolvedCSSPropertyID(const CSSParserString&);
CSSValueID cssValueKeywordID(const CSSParserString&);

} // namespace blink

#endif // CSSPropertyParser_h
