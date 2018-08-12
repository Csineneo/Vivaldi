// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ObjectPaintProperties_h
#define ObjectPaintProperties_h

#include "platform/geometry/LayoutPoint.h"
#include "platform/graphics/paint/ClipPaintPropertyNode.h"
#include "platform/graphics/paint/EffectPaintPropertyNode.h"
#include "platform/graphics/paint/PaintChunkProperties.h"
#include "platform/graphics/paint/TransformPaintPropertyNode.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"

namespace blink {

// This class stores property tree related information associated with a LayoutObject.
// Currently there are two groups of information:
// 1. The set of property nodes created locally by this LayoutObject.
// 2. [Optional] A suite of property nodes (PaintChunkProperties) and paint offset
//    that can be used to paint the border box of this LayoutObject.
class ObjectPaintProperties {
    WTF_MAKE_NONCOPYABLE(ObjectPaintProperties);
    USING_FAST_MALLOC(ObjectPaintProperties);
public:
    struct LocalBorderBoxProperties;

    // TODO(pdr): This should be refactored to not pass 11 arguments because it
    // is hard to follow here and at the callsites.
    static PassOwnPtr<ObjectPaintProperties> create(
        PassRefPtr<TransformPaintPropertyNode> paintOffsetTranslation,
        PassRefPtr<TransformPaintPropertyNode> transform,
        PassRefPtr<EffectPaintPropertyNode> effect,
        PassRefPtr<ClipPaintPropertyNode> cssClip,
        PassRefPtr<ClipPaintPropertyNode> cssClipFixedPosition,
        PassRefPtr<ClipPaintPropertyNode> overflowClip,
        PassRefPtr<TransformPaintPropertyNode> perspective,
        PassRefPtr<TransformPaintPropertyNode> svgLocalTransform,
        PassRefPtr<TransformPaintPropertyNode> scrollTranslation,
        PassRefPtr<TransformPaintPropertyNode> scrollbarPaintOffset,
        PassOwnPtr<LocalBorderBoxProperties> localBorderBoxProperties)
    {
        return adoptPtr(new ObjectPaintProperties(paintOffsetTranslation,
            transform, effect, cssClip, cssClipFixedPosition, overflowClip,
            perspective, svgLocalTransform, scrollTranslation,
            scrollbarPaintOffset, localBorderBoxProperties));
    }

    // The hierarchy of transform subtree created by a LayoutObject.
    // [ paintOffsetTranslation ]           Normally paint offset is accumulated without creating a node
    // |                                    until we see, for example, transform or position:fixed.
    // +---[ transform ]                    The space created by CSS transform.
    //     |                                This is the local border box space, see: LocalBorderBoxProperties below.
    //     +---[ perspective ]              The space created by CSS perspective.
    //     |   +---[ svgLocalTransform ]    The transform for an SVG element.
    //     |              OR                (SVG does not support scrolling.)
    //     |   +---[ scrollTranslation ]    The space created by overflow clip.
    //     +---[ scrollbarPaintOffset ]     TODO(trchen): Remove this once we bake the paint offset into frameRect.
    //                                      This is equivalent to the local border box space above,
    //                                      with pixel snapped paint offset baked in. It is really redundant,
    //                                      but it is a pain to teach scrollbars to paint with an offset.
    TransformPaintPropertyNode* paintOffsetTranslation() const { return m_paintOffsetTranslation.get(); }
    TransformPaintPropertyNode* transform() const { return m_transform.get(); }
    TransformPaintPropertyNode* perspective() const { return m_perspective.get(); }
    TransformPaintPropertyNode* svgLocalTransform() const { return m_svgLocalTransform.get(); }
    TransformPaintPropertyNode* scrollTranslation() const { return m_scrollTranslation.get(); }
    TransformPaintPropertyNode* scrollbarPaintOffset() const { return m_scrollbarPaintOffset.get(); }

    EffectPaintPropertyNode* effect() const { return m_effect.get(); }

    ClipPaintPropertyNode* cssClip() const { return m_cssClip.get(); }
    ClipPaintPropertyNode* cssClipFixedPosition() const { return m_cssClipFixedPosition.get(); }
    ClipPaintPropertyNode* overflowClip() const { return m_overflowClip.get(); }

    // This is a complete set of property nodes that should be used as a starting point to paint
    // this layout object. It is needed becauase some property inherits from the containing block,
    // not painting parent, thus can't be derived in O(1) during paint walk.
    // Note: If this layout object has transform or stacking-context effects, those are already
    // baked into in the context here. However for properties that affects only children,
    // for example, perspective and overflow clip, those should be applied by the painter
    // at the right painting step.
    struct LocalBorderBoxProperties {
        LayoutPoint paintOffset;
        RefPtr<TransformPaintPropertyNode> transform;
        RefPtr<ClipPaintPropertyNode> clip;
        RefPtr<EffectPaintPropertyNode> effect;
    };
    LocalBorderBoxProperties* localBorderBoxProperties() const { return m_localBorderBoxProperties.get(); }

private:
    ObjectPaintProperties(
        PassRefPtr<TransformPaintPropertyNode> paintOffsetTranslation,
        PassRefPtr<TransformPaintPropertyNode> transform,
        PassRefPtr<EffectPaintPropertyNode> effect,
        PassRefPtr<ClipPaintPropertyNode> cssClip,
        PassRefPtr<ClipPaintPropertyNode> cssClipFixedPosition,
        PassRefPtr<ClipPaintPropertyNode> overflowClip,
        PassRefPtr<TransformPaintPropertyNode> perspective,
        PassRefPtr<TransformPaintPropertyNode> svgLocalTransform,
        PassRefPtr<TransformPaintPropertyNode> scrollTranslation,
        PassRefPtr<TransformPaintPropertyNode> scrollbarPaintOffset,
        PassOwnPtr<LocalBorderBoxProperties> localBorderBoxProperties)
        : m_paintOffsetTranslation(paintOffsetTranslation)
        , m_transform(transform)
        , m_effect(effect)
        , m_cssClip(cssClip)
        , m_cssClipFixedPosition(cssClipFixedPosition)
        , m_overflowClip(overflowClip)
        , m_perspective(perspective)
        , m_svgLocalTransform(svgLocalTransform)
        , m_scrollTranslation(scrollTranslation)
        , m_scrollbarPaintOffset(scrollbarPaintOffset)
        , m_localBorderBoxProperties(localBorderBoxProperties) { }

    RefPtr<TransformPaintPropertyNode> m_paintOffsetTranslation;
    RefPtr<TransformPaintPropertyNode> m_transform;
    RefPtr<EffectPaintPropertyNode> m_effect;
    RefPtr<ClipPaintPropertyNode> m_cssClip;
    RefPtr<ClipPaintPropertyNode> m_cssClipFixedPosition;
    RefPtr<ClipPaintPropertyNode> m_overflowClip;
    RefPtr<TransformPaintPropertyNode> m_perspective;
    RefPtr<TransformPaintPropertyNode> m_svgLocalTransform;
    RefPtr<TransformPaintPropertyNode> m_scrollTranslation;
    RefPtr<TransformPaintPropertyNode> m_scrollbarPaintOffset;

    OwnPtr<LocalBorderBoxProperties> m_localBorderBoxProperties;
};

} // namespace blink

#endif // ObjectPaintProperties_h
