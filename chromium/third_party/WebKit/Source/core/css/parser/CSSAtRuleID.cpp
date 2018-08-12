// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/parser/CSSAtRuleID.h"

#include "core/css/parser/CSSParserString.h"
#include "core/frame/UseCounter.h"

namespace blink {

CSSAtRuleID cssAtRuleID(const CSSParserString& name)
{
    if (name.equalIgnoringASCIICase("charset"))
        return CSSAtRuleCharset;
    if (name.equalIgnoringASCIICase("font-face"))
        return CSSAtRuleFontFace;
    if (name.equalIgnoringASCIICase("import"))
        return CSSAtRuleImport;
    if (name.equalIgnoringASCIICase("keyframes"))
        return CSSAtRuleKeyframes;
    if (name.equalIgnoringASCIICase("media"))
        return CSSAtRuleMedia;
    if (name.equalIgnoringASCIICase("namespace"))
        return CSSAtRuleNamespace;
    if (name.equalIgnoringASCIICase("page"))
        return CSSAtRulePage;
    if (name.equalIgnoringASCIICase("supports"))
        return CSSAtRuleSupports;
    if (name.equalIgnoringASCIICase("viewport"))
        return CSSAtRuleViewport;
    if (name.equalIgnoringASCIICase("-webkit-keyframes"))
        return CSSAtRuleWebkitKeyframes;
    if (name.equalIgnoringASCIICase("apply"))
        return CSSAtRuleApply;
    return CSSAtRuleInvalid;
}

void countAtRule(UseCounter* useCounter, CSSAtRuleID ruleId)
{
    ASSERT(useCounter);
    UseCounter::Feature feature;

    switch (ruleId) {
    case CSSAtRuleCharset:
        feature = UseCounter::CSSAtRuleCharset;
        break;
    case CSSAtRuleFontFace:
        feature = UseCounter::CSSAtRuleFontFace;
        break;
    case CSSAtRuleImport:
        feature = UseCounter::CSSAtRuleImport;
        break;
    case CSSAtRuleKeyframes:
        feature = UseCounter::CSSAtRuleKeyframes;
        break;
    case CSSAtRuleMedia:
        feature = UseCounter::CSSAtRuleMedia;
        break;
    case CSSAtRuleNamespace:
        feature = UseCounter::CSSAtRuleNamespace;
        break;
    case CSSAtRulePage:
        feature = UseCounter::CSSAtRulePage;
        break;
    case CSSAtRuleSupports:
        feature = UseCounter::CSSAtRuleSupports;
        break;
    case CSSAtRuleViewport:
        feature = UseCounter::CSSAtRuleViewport;
        break;

    case CSSAtRuleWebkitKeyframes:
        feature = UseCounter::CSSAtRuleWebkitKeyframes;
        break;

    case CSSAtRuleApply:
        feature = UseCounter::CSSAtRuleApply;
        break;

    case CSSAtRuleInvalid:
        // fallthrough
    default:
        ASSERT_NOT_REACHED();
        return;
    }
    useCounter->count(feature);
}

} // namespace blink

