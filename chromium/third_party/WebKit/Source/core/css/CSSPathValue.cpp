// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/CSSPathValue.h"

#include "core/style/StylePath.h"
#include "core/svg/SVGPathUtilities.h"

namespace blink {

PassRefPtrWillBeRawPtr<CSSPathValue> CSSPathValue::create(PassRefPtr<StylePath> stylePath)
{
    return adoptRefWillBeNoop(new CSSPathValue(stylePath));
}

PassRefPtrWillBeRawPtr<CSSPathValue> CSSPathValue::create(PassOwnPtr<SVGPathByteStream> pathByteStream)
{
    return CSSPathValue::create(StylePath::create(pathByteStream));
}

CSSPathValue::CSSPathValue(PassRefPtr<StylePath> stylePath)
    : CSSValue(PathClass)
    , m_stylePath(stylePath)
{
    ASSERT(m_stylePath);
}

CSSPathValue::~CSSPathValue()
{
}

namespace {

PassRefPtrWillBeRawPtr<CSSPathValue> createPathValue()
{
    OwnPtr<SVGPathByteStream> pathByteStream = SVGPathByteStream::create();
    // Need to be registered as LSan ignored, as it will be reachable and
    // separately referred to by emptyPathValue() callers.
    LEAK_SANITIZER_IGNORE_OBJECT(pathByteStream.get());
    return CSSPathValue::create(pathByteStream.release());
}

} // namespace

CSSPathValue* CSSPathValue::emptyPathValue()
{
    DEFINE_STATIC_LOCAL(RefPtrWillBePersistent<CSSPathValue>, empty, (createPathValue()));
    return empty.get();
}

StylePath* CSSPathValue::stylePath() const
{
    return m_stylePath.get();
}

String CSSPathValue::customCSSText() const
{
    return "path('" + pathString() + "')";
}

bool CSSPathValue::equals(const CSSPathValue& other) const
{
    return byteStream() == other.byteStream();
}

DEFINE_TRACE_AFTER_DISPATCH(CSSPathValue)
{
    CSSValue::traceAfterDispatch(visitor);
}

String CSSPathValue::pathString() const
{
    return buildStringFromByteStream(byteStream());
}

} // namespace blink
