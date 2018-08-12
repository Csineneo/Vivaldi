// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/PaintLayerScrollableAreaTest.h"

#include "platform/graphics/GraphicsLayer.h"

namespace blink {

TEST_F(PaintLayerScrollableAreaTest,
       CanPaintBackgroundOntoScrollingContentsLayer) {
  document().frame()->settings()->setPreferCompositingToLCDTextEnabled(true);
  setBodyInnerHTML(
      "<style>"
      ".scroller { overflow: scroll; will-change: transform; width: 300px; "
      "height: 300px;} .spacer { height: 1000px; }"
      "#scroller13::-webkit-scrollbar { width: 13px; height: 13px;}"
      "</style>"
      "<div id='scroller1' class='scroller' style='background: white local;'>"
      "    <div id='negative-composited-child' style='background-color: red; "
      "width: 1px; height: 1px; position: absolute; backface-visibility: "
      "hidden; z-index: -1'></div>"
      "    <div class='spacer'></div>"
      "</div>"
      "<div id='scroller2' class='scroller' style='background: white "
      "content-box; padding: 10px;'><div class='spacer'></div></div>"
      "<div id='scroller3' class='scroller' style='background: white local "
      "content-box; padding: 10px;'><div class='spacer'></div></div>"
      "<div id='scroller4' class='scroller' style='background: "
      "url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUg), white local;'><div "
      "class='spacer'></div></div>"
      "<div id='scroller5' class='scroller' style='background: "
      "url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUg) local, white "
      "local;'><div class='spacer'></div></div>"
      "<div id='scroller6' class='scroller' style='background: "
      "url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUg) local, white "
      "padding-box; padding: 10px;'><div class='spacer'></div></div>"
      "<div id='scroller7' class='scroller' style='background: "
      "url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUg) local, white "
      "content-box; padding: 10px;'><div class='spacer'></div></div>"
      "<div id='scroller8' class='scroller' style='background: white "
      "border-box;'><div class='spacer'></div></div>"
      "<div id='scroller9' class='scroller' style='background: white "
      "border-box; border: 10px solid black;'><div class='spacer'></div></div>"
      "<div id='scroller10' class='scroller' style='background: white "
      "border-box; border: 10px solid rgba(0, 0, 0, 0.5);'><div "
      "class='spacer'></div></div>"
      "<div id='scroller11' class='scroller' style='background: white "
      "content-box;'><div class='spacer'></div></div>"
      "<div id='scroller12' class='scroller' style='background: white "
      "content-box; padding: 10px;'><div class='spacer'></div></div>"
      "<div id='scroller13' class='scroller' style='background: white "
      "border-box;'><div class='spacer'></div></div>"
      "<div id='scroller14' class='scroller' style='background: white; border: "
      "1px solid black; outline: 1px solid blue; outline-offset: -1px;'><div "
      "class='spacer'></div></div>"
      "<div id='scroller15' class='scroller' style='background: white; border: "
      "1px solid black; outline: 1px solid blue; outline-offset: -2px;'><div "
      "class='spacer'></div></div>"
      "<div id='scroller16' class='scroller' style='background: white; clip: "
      "rect(0px,10px,10px,0px);'><div class='spacer'></div></div>");

  // #scroller1 cannot paint background into scrolling contents layer because it
  // has a negative z-index child.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller1"));

  // #scroller2 cannot paint background into scrolling contents layer because it
  // has a content-box clip without local attachment.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller2"));

  // #scroller3 can paint background into scrolling contents layer.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller3"));

  // #scroller4 cannot paint background into scrolling contents layer because
  // the background image is not locally attached.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller4"));

  // #scroller5 can paint background into scrolling contents layer because both
  // the image and color are locally attached.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller5"));

  // #scroller6 can paint background into scrolling contents layer because the
  // image is locally attached and even though the color is not, it is filled to
  // the padding box so it will be drawn the same as a locally attached
  // background.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller6"));

  // #scroller7 cannot paint background into scrolling contents layer because
  // the color is filled to the content box and we have padding so it is not
  // equivalent to a locally attached background.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller7"));

  // #scroller8 can paint background into scrolling contents layer because its
  // border-box is equivalent to its padding box since it has no border.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller8"));

  // #scroller9 can paint background into scrolling contents layer because its
  // border is opaque so it completely covers the background outside of the
  // padding-box.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller9"));

  // #scroller10 cannot paint background into scrolling contents layer because
  // its border is partially transparent so the background must be drawn to the
  // border-box edges.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller10"));

  // #scroller11 can paint background into scrolling contents layer because its
  // content-box is equivalent to its padding box since it has no padding.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller11"));

  // #scroller12 cannot paint background into scrolling contents layer because
  // it has padding so its content-box is not equivalent to its padding-box.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller12"));

  // #scroller13 cannot paint background into scrolling contents layer because
  // it has a custom scrollbar which the background may need to draw under.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller13"));

  // #scroller14 can paint background into scrolling contents layer because the
  // outline is drawn outside the padding box.
  EXPECT_TRUE(canPaintBackgroundOntoScrollingContentsLayer("scroller14"));

  // #scroller15 cannot paint background into scrolling contents layer because
  // the outline is drawn inside the padding box.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller15"));

  // #scroller16 cannot paint background into scrolling contents layer because
  // the scroller has a clip which would not be respected by the scrolling
  // contents layer.
  EXPECT_FALSE(canPaintBackgroundOntoScrollingContentsLayer("scroller16"));
}

TEST_F(PaintLayerScrollableAreaTest, OpaqueLayersPromoted) {
  RuntimeEnabledFeatures::setCompositeOpaqueScrollersEnabled(true);

  setBodyInnerHTML(
      "<style>"
      "#scroller { overflow: scroll; height: 200px; width: 200px; background: "
      "white local content-box; border: 10px solid rgba(0, 255, 0, 0.5); }"
      "#scrolled { height: 300px; }"
      "</style>"
      "<div id=\"scroller\"><div id=\"scrolled\"></div></div>");
  document().view()->updateAllLifecyclePhases();

  EXPECT_TRUE(RuntimeEnabledFeatures::compositeOpaqueScrollersEnabled());
  Element* scroller = document().getElementById("scroller");
  PaintLayer* paintLayer =
      toLayoutBoxModelObject(scroller->layoutObject())->layer();
  ASSERT_TRUE(paintLayer);
  EXPECT_TRUE(paintLayer->needsCompositedScrolling());
  EXPECT_TRUE(paintLayer->graphicsLayerBacking());
  ASSERT_TRUE(paintLayer->graphicsLayerBackingForScrolling());
  EXPECT_TRUE(paintLayer->graphicsLayerBackingForScrolling()->contentsOpaque());
}

TEST_F(PaintLayerScrollableAreaTest, TransparentLayersNotPromoted) {
  RuntimeEnabledFeatures::setCompositeOpaqueScrollersEnabled(true);

  setBodyInnerHTML(
      "<style>"
      "#scroller { overflow: scroll; height: 200px; width: 200px; background: "
      "rgba(0, 255, 0, 0.5) local content-box; border: 10px solid rgba(0, 255, "
      "0, 0.5); }"
      "#scrolled { height: 300px; }"
      "</style>"
      "<div id=\"scroller\"><div id=\"scrolled\"></div></div>");
  document().view()->updateAllLifecyclePhases();

  EXPECT_TRUE(RuntimeEnabledFeatures::compositeOpaqueScrollersEnabled());
  Element* scroller = document().getElementById("scroller");
  PaintLayer* paintLayer =
      toLayoutBoxModelObject(scroller->layoutObject())->layer();
  ASSERT_TRUE(paintLayer);
  EXPECT_FALSE(paintLayer->needsCompositedScrolling());
  EXPECT_FALSE(paintLayer->graphicsLayerBacking());
  EXPECT_FALSE(paintLayer->graphicsLayerBackingForScrolling());
}

TEST_F(PaintLayerScrollableAreaTest, OpaqueLayersDepromotedOnStyleChange) {
  RuntimeEnabledFeatures::setCompositeOpaqueScrollersEnabled(true);

  setBodyInnerHTML(
      "<style>"
      "#scroller { overflow: scroll; height: 200px; width: 200px; background: "
      "white local content-box; }"
      "#scrolled { height: 300px; }"
      "</style>"
      "<div id=\"scroller\"><div id=\"scrolled\"></div></div>");
  document().view()->updateAllLifecyclePhases();

  EXPECT_TRUE(RuntimeEnabledFeatures::compositeOpaqueScrollersEnabled());
  Element* scroller = document().getElementById("scroller");
  PaintLayer* paintLayer =
      toLayoutBoxModelObject(scroller->layoutObject())->layer();
  ASSERT_TRUE(paintLayer);
  EXPECT_TRUE(paintLayer->needsCompositedScrolling());

  // Change the background to transparent
  scroller->setAttribute(
      HTMLNames::styleAttr,
      "background: rgba(255,255,255,0.5) local content-box;");
  document().view()->updateAllLifecyclePhases();
  paintLayer = toLayoutBoxModelObject(scroller->layoutObject())->layer();
  ASSERT_TRUE(paintLayer);
  EXPECT_FALSE(paintLayer->needsCompositedScrolling());
  EXPECT_FALSE(paintLayer->graphicsLayerBacking());
  EXPECT_FALSE(paintLayer->graphicsLayerBackingForScrolling());
}

TEST_F(PaintLayerScrollableAreaTest, OpaqueLayersPromotedOnStyleChange) {
  RuntimeEnabledFeatures::setCompositeOpaqueScrollersEnabled(true);

  setBodyInnerHTML(
      "<style>"
      "#scroller { overflow: scroll; height: 200px; width: 200px; background: "
      "rgba(255,255,255,0.5) local content-box; }"
      "#scrolled { height: 300px; }"
      "</style>"
      "<div id=\"scroller\"><div id=\"scrolled\"></div></div>");
  document().view()->updateAllLifecyclePhases();

  EXPECT_TRUE(RuntimeEnabledFeatures::compositeOpaqueScrollersEnabled());
  Element* scroller = document().getElementById("scroller");
  PaintLayer* paintLayer =
      toLayoutBoxModelObject(scroller->layoutObject())->layer();
  ASSERT_TRUE(paintLayer);
  EXPECT_FALSE(paintLayer->needsCompositedScrolling());

  // Change the background to transparent
  scroller->setAttribute(HTMLNames::styleAttr,
                         "background: white local content-box;");
  document().view()->updateAllLifecyclePhases();
  paintLayer = toLayoutBoxModelObject(scroller->layoutObject())->layer();
  ASSERT_TRUE(paintLayer);
  EXPECT_TRUE(paintLayer->needsCompositedScrolling());
  EXPECT_TRUE(paintLayer->graphicsLayerBacking());
  ASSERT_TRUE(paintLayer->graphicsLayerBackingForScrolling());
  EXPECT_TRUE(paintLayer->graphicsLayerBackingForScrolling()->contentsOpaque());
}
}
