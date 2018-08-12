// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/NinePieceImagePainter.h"

#include "core/frame/Deprecation.h"
#include "core/layout/ImageQualityController.h"
#include "core/layout/LayoutBoxModelObject.h"
#include "core/paint/BoxPainter.h"
#include "core/paint/NinePieceImageGrid.h"
#include "core/style/ComputedStyle.h"
#include "core/style/NinePieceImage.h"
#include "platform/geometry/IntSize.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/GraphicsContext.h"

namespace blink {

NinePieceImagePainter::NinePieceImagePainter(const LayoutBoxModelObject& layoutObject)
    : m_layoutObject(layoutObject)
{
}

bool NinePieceImagePainter::paint(GraphicsContext& graphicsContext, const LayoutRect& rect, const ComputedStyle& style,
    const NinePieceImage& ninePieceImage, SkXfermode::Mode op) const
{
    StyleImage* styleImage = ninePieceImage.image();
    if (!styleImage)
        return false;

    if (!styleImage->isLoaded())
        return true; // Never paint a nine-piece image incrementally, but don't paint the fallback borders either.

    if (!styleImage->canRender())
        return false;

    // Find out if the hasImage() check in ComputedStyle::border*Width had any affect, i.e. if a border is non-zero while border-style is
    // none or hidden.
    if ((style.borderLeftWidth() && (style.borderLeft().style() == BorderStyleNone || style.borderLeft().style() == BorderStyleHidden))
        || (style.borderRightWidth() && (style.borderRight().style() == BorderStyleNone || style.borderRight().style() == BorderStyleHidden))
        || (style.borderTopWidth() && (style.borderTop().style() == BorderStyleNone || style.borderTop().style() == BorderStyleHidden))
        || (style.borderBottomWidth() && (style.borderBottom().style() == BorderStyleNone || style.borderBottom().style() == BorderStyleHidden)))
        Deprecation::countDeprecation(m_layoutObject.document(), UseCounter::BorderImageWithBorderStyleNone);

    // FIXME: border-image is broken with full page zooming when tiling has to happen, since the tiling function
    // doesn't have any understanding of the zoom that is in effect on the tile.
    LayoutRect rectWithOutsets = rect;
    rectWithOutsets.expand(style.imageOutsets(ninePieceImage));
    LayoutRect borderImageRect = rectWithOutsets;

    // NinePieceImage returns the image slices without effective zoom applied and thus we compute
    // the nine piece grid on top of the image in unzoomed coordinates.
    //
    // FIXME: The default object size passed to imageSize() should be scaled by the zoom factor
    // passed in. In this case it means that borderImageRect should be passed in compensated by
    // effective zoom, since the scale factor is one. For generated images, the actual image data
    // (gradient stops, etc.) are scaled to effective zoom instead so we must take care not to cause
    // scale of them again.
    IntSize imageSize = roundedIntSize(styleImage->imageSize(m_layoutObject, 1, borderImageRect.size()));

    IntRectOutsets borderWidths(style.borderTopWidth(), style.borderRightWidth(),
        style.borderBottomWidth(), style.borderLeftWidth());
    NinePieceImageGrid grid(ninePieceImage, imageSize, pixelSnappedIntRect(borderImageRect), borderWidths);

    RefPtr<Image> image = styleImage->image(m_layoutObject, imageSize, style.effectiveZoom());

    InterpolationQuality interpolationQuality = BoxPainter::chooseInterpolationQuality(m_layoutObject, image.get(), 0, rectWithOutsets.size());
    InterpolationQuality previousInterpolationQuality = graphicsContext.imageInterpolationQuality();
    graphicsContext.setImageInterpolationQuality(interpolationQuality);

    TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("devtools.timeline"), "PaintImage", "data",
        InspectorPaintImageEvent::data(m_layoutObject, *styleImage));

    for (NinePiece piece = MinPiece; piece < MaxPiece; ++piece) {
        NinePieceImageGrid::NinePieceDrawInfo drawInfo = grid.getNinePieceDrawInfo(piece, styleImage->imageScaleFactor());

        if (drawInfo.isDrawable) {
            if (drawInfo.isCornerPiece) {
                graphicsContext.drawImage(image.get(), drawInfo.destination, drawInfo.source, op);
            } else {
                graphicsContext.drawTiledImage(image.get(), drawInfo.destination,
                    drawInfo.source, drawInfo.tileScale, drawInfo.tileRule.horizontal,
                    drawInfo.tileRule.vertical, op);
            }
        }
    }

    graphicsContext.setImageInterpolationQuality(previousInterpolationQuality);
    return true;
}

} // namespace blink
