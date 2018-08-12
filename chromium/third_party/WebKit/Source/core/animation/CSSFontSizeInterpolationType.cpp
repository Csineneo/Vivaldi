// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/animation/CSSFontSizeInterpolationType.h"

#include "core/animation/CSSLengthInterpolationType.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/resolver/StyleResolverState.h"
#include "platform/LengthFunctions.h"
#include "platform/fonts/FontDescription.h"

namespace blink {

namespace {

class InheritedFontSizeChecker : public InterpolationType::ConversionChecker {
public:
    static PassOwnPtr<InheritedFontSizeChecker> create(const FontDescription::Size& inheritedFontSize)
    {
        return adoptPtr(new InheritedFontSizeChecker(inheritedFontSize));
    }

private:
    InheritedFontSizeChecker(const FontDescription::Size& inheritedFontSize)
        : m_inheritedFontSize(inheritedFontSize.value)
    { }

    bool isValid(const InterpolationEnvironment& environment, const InterpolationValue&) const final
    {
        return m_inheritedFontSize == environment.state().parentFontDescription().getSize().value;
    }

    const float m_inheritedFontSize;
};

InterpolationValue convertFontSize(float size)
{
    return InterpolationValue(CSSLengthInterpolationType::createInterpolablePixels(size));
}

InterpolationValue maybeConvertKeyword(CSSValueID valueID, const StyleResolverState& state, InterpolationType::ConversionCheckers* conversionCheckers)
{
    if (FontSize::isValidValueID(valueID)) {
        // TODO(alancutter): Be responsive to changes in isMonospace().
        return convertFontSize(state.fontBuilder().fontSizeForKeyword(FontSize::keywordSize(valueID), state.style()->getFontDescription().isMonospace()));
    }

    if (valueID != CSSValueSmaller && valueID != CSSValueLarger)
        return nullptr;

    const FontDescription::Size& inheritedFontSize = state.parentFontDescription().getSize();
    conversionCheckers->append(InheritedFontSizeChecker::create(inheritedFontSize));
    if (valueID == CSSValueSmaller)
        return convertFontSize(FontDescription::smallerSize(inheritedFontSize).value);
    return convertFontSize(FontDescription::largerSize(inheritedFontSize).value);
}

} // namespace

InterpolationValue CSSFontSizeInterpolationType::maybeConvertNeutral(const InterpolationValue&, ConversionCheckers&) const
{
    return InterpolationValue(CSSLengthInterpolationType::createNeutralInterpolableValue());
}

InterpolationValue CSSFontSizeInterpolationType::maybeConvertInitial(const StyleResolverState& state) const
{
    return maybeConvertKeyword(FontSize::initialValueID(), state, nullptr);
}

InterpolationValue CSSFontSizeInterpolationType::maybeConvertInherit(const StyleResolverState& state, ConversionCheckers& conversionCheckers) const
{
    const FontDescription::Size& inheritedFontSize = state.parentFontDescription().getSize();
    conversionCheckers.append(InheritedFontSizeChecker::create(inheritedFontSize));
    return convertFontSize(inheritedFontSize.value);
}

InterpolationValue CSSFontSizeInterpolationType::maybeConvertValue(const CSSValue& value, const StyleResolverState& state, ConversionCheckers& conversionCheckers) const
{
    OwnPtr<InterpolableValue> result = CSSLengthInterpolationType::maybeConvertCSSValue(value).interpolableValue.release();
    if (result)
        return InterpolationValue(result.release());

    if (!value.isPrimitiveValue() || !toCSSPrimitiveValue(value).isValueID())
        return nullptr;

    return maybeConvertKeyword(toCSSPrimitiveValue(value).getValueID(), state, &conversionCheckers);
}

InterpolationValue CSSFontSizeInterpolationType::maybeConvertUnderlyingValue(const InterpolationEnvironment& environment) const
{
    return convertFontSize(environment.state().style()->specifiedFontSize());
}

void CSSFontSizeInterpolationType::apply(const InterpolableValue& interpolableValue, const NonInterpolableValue*, InterpolationEnvironment& environment) const
{
    const FontDescription& parentFont = environment.state().parentFontDescription();
    Length fontSizeLength = CSSLengthInterpolationType::resolveInterpolableLength(interpolableValue, nullptr, environment.state().fontSizeConversionData(), ValueRangeNonNegative);
    float fontSize = floatValueForLength(fontSizeLength, parentFont.getSize().value);
    environment.state().fontBuilder().setSize(FontDescription::Size(0, fontSize, !fontSizeLength.hasPercent() || parentFont.isAbsoluteSize()));
}

} // namespace blink
