// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_block_layout_algorithm.h"

#include "core/layout/ng/ng_box.h"
#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_physical_fragment.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_units.h"
#include "core/style/ComputedStyle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

class NGBlockLayoutAlgorithmTest : public ::testing::Test {
 protected:
  void SetUp() override { style_ = ComputedStyle::create(); }

  NGPhysicalFragment* RunBlockLayoutAlgorithm(const NGConstraintSpace* space,
                                              NGBox* first_child) {
    NGBlockLayoutAlgorithm algorithm(style_, first_child);
    NGPhysicalFragment* frag;
    while (!algorithm.Layout(space, &frag))
      continue;
    return frag;
  }

  RefPtr<ComputedStyle> style_;
};

TEST_F(NGBlockLayoutAlgorithmTest, FixedSize) {
  style_->setWidth(Length(30, Fixed));
  style_->setHeight(Length(40, Fixed));

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, nullptr);

  EXPECT_EQ(LayoutUnit(30), frag->Width());
  EXPECT_EQ(LayoutUnit(40), frag->Height());
}

// Verifies that two children are laid out with the correct size and position.
TEST_F(NGBlockLayoutAlgorithmTest, LayoutBlockChildren) {
  const int kWidth = 30;
  const int kHeight1 = 20;
  const int kHeight2 = 30;
  const int kMarginTop = 5;
  const int kMarginBottom = 20;
  style_->setWidth(Length(kWidth, Fixed));

  RefPtr<ComputedStyle> first_style = ComputedStyle::create();
  first_style->setHeight(Length(kHeight1, Fixed));
  NGBox* first_child = new NGBox(first_style.get());

  RefPtr<ComputedStyle> second_style = ComputedStyle::create();
  second_style->setHeight(Length(kHeight2, Fixed));
  second_style->setMarginTop(Length(kMarginTop, Fixed));
  second_style->setMarginBottom(Length(kMarginBottom, Fixed));
  NGBox* second_child = new NGBox(second_style.get());

  first_child->SetNextSibling(second_child);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, first_child);

  EXPECT_EQ(LayoutUnit(kWidth), frag->Width());
  EXPECT_EQ(LayoutUnit(kHeight1 + kHeight2 + kMarginTop), frag->Height());
  EXPECT_EQ(NGPhysicalFragmentBase::FragmentBox, frag->Type());
  ASSERT_EQ(frag->Children().size(), 2UL);

  const NGPhysicalFragmentBase* child = frag->Children()[0];
  EXPECT_EQ(kHeight1, child->Height());
  EXPECT_EQ(0, child->TopOffset());

  child = frag->Children()[1];
  EXPECT_EQ(kHeight2, child->Height());
  EXPECT_EQ(kHeight1 + kMarginTop, child->TopOffset());
}

// Verifies that a child is laid out correctly if it's writing mode is different
// from the parent's one.
//
// Test case's HTML representation:
// <div style="writing-mode: vertical-lr;">
//   <div style="width:50px;
//       height: 50px; margin-left: 100px;
//       writing-mode: horizontal-tb;"></div>
// </div>
TEST_F(NGBlockLayoutAlgorithmTest, LayoutBlockChildrenWithWritingMode) {
  const int kWidth = 50;
  const int kHeight = 50;
  const int kMarginLeft = 100;

  RefPtr<ComputedStyle> div1_style = ComputedStyle::create();
  div1_style->setWritingMode(LeftToRightWritingMode);
  NGBox* div1 = new NGBox(div1_style.get());

  RefPtr<ComputedStyle> div2_style = ComputedStyle::create();
  div2_style->setHeight(Length(kHeight, Fixed));
  div2_style->setWidth(Length(kWidth, Fixed));
  div1_style->setWritingMode(TopToBottomWritingMode);
  div2_style->setMarginLeft(Length(kMarginLeft, Fixed));
  NGBox* div2 = new NGBox(div2_style.get());

  div1->SetFirstChild(div2);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(500), LayoutUnit(500)));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, div1);

  const NGPhysicalFragmentBase* child = frag->Children()[0];
  // DIV2
  child = static_cast<const NGPhysicalFragment*>(child)->Children()[0];

  EXPECT_EQ(kHeight, child->Height());
  EXPECT_EQ(0, child->TopOffset());
  EXPECT_EQ(kMarginLeft, child->LeftOffset());
}

// Verifies the collapsing margins case for the next pair:
// - top margin of a box and top margin of its first in-flow child.
//
// Test case's HTML representation:
// <div style="margin-top: 20px; height: 50px;">  <!-- DIV1 -->
//    <div style="margin-top: 10px"></div>        <!-- DIV2 -->
// </div>
//
// Expected:
// - Empty margin strut of the fragment that establishes new formatting context
// - Margins are collapsed resulting a single margin 20px = max(20px, 10px)
// - The top offset of DIV2 == 20px
TEST_F(NGBlockLayoutAlgorithmTest, CollapsingMarginsCase1) {
  const int kHeight = 50;
  const int kDiv1MarginTop = 20;
  const int kDiv2MarginTop = 10;

  // DIV1
  RefPtr<ComputedStyle> div1_style = ComputedStyle::create();
  div1_style->setHeight(Length(kHeight, Fixed));
  div1_style->setMarginTop(Length(kDiv1MarginTop, Fixed));
  NGBox* div1 = new NGBox(div1_style.get());

  // DIV2
  RefPtr<ComputedStyle> div2_style = ComputedStyle::create();
  div2_style->setMarginTop(Length(kDiv2MarginTop, Fixed));
  NGBox* div2 = new NGBox(div2_style.get());

  div1->SetFirstChild(div2);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  space->SetIsNewFormattingContext(true);
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, div1);

  EXPECT_TRUE(frag->MarginStrut().IsEmpty());
  ASSERT_EQ(frag->Children().size(), 1UL);
  const NGPhysicalFragmentBase* div2_fragment = frag->Children()[0];
  EXPECT_EQ(NGMarginStrut({LayoutUnit(kDiv2MarginTop)}),
            div2_fragment->MarginStrut());
  EXPECT_EQ(kDiv1MarginTop, div2_fragment->TopOffset());
}

// Verifies the collapsing margins case for the next pair:
// - bottom margin of box and top margin of its next in-flow following sibling.
//
// Test case's HTML representation:
// <div style="margin-bottom: 20px; height: 50px;">  <!-- DIV1 -->
//    <div style="margin-bottom: -15px"></div>       <!-- DIV2 -->
//    <div></div>                                    <!-- DIV3 -->
// </div>
// <div></div>                                       <!-- DIV4 -->
// <div style="margin-top: 10px; height: 50px;">     <!-- DIV5 -->
//    <div></div>                                    <!-- DIV6 -->
//    <div style="margin-top: -30px"></div>          <!-- DIV7 -->
// </div>
//
// Expected:
//   Margins are collapsed resulting an overlap
//   -10px = max(20px, 10px) - max(abs(-15px), abs(-30px))
//   between DIV2 and DIV3. Zero-height blocks are ignored.
TEST_F(NGBlockLayoutAlgorithmTest, CollapsingMarginsCase2) {
  const int kHeight = 50;
  const int kDiv1MarginBottom = 20;
  const int kDiv2MarginBottom = -15;
  const int kDiv5MarginTop = 10;
  const int kDiv7MarginTop = -30;
  const int kExpectedCollapsedMargin = -10;

  // DIV1
  RefPtr<ComputedStyle> div1_style = ComputedStyle::create();
  div1_style->setHeight(Length(kHeight, Fixed));
  div1_style->setMarginBottom(Length(kDiv1MarginBottom, Fixed));
  NGBox* div1 = new NGBox(div1_style.get());

  // DIV2
  RefPtr<ComputedStyle> div2_style = ComputedStyle::create();
  div2_style->setMarginBottom(Length(kDiv2MarginBottom, Fixed));
  NGBox* div2 = new NGBox(div2_style.get());

  // Empty DIVs: DIV3, DIV4, DIV6
  NGBox* div3 = new NGBox(ComputedStyle::create().get());
  NGBox* div4 = new NGBox(ComputedStyle::create().get());
  NGBox* div6 = new NGBox(ComputedStyle::create().get());

  // DIV5
  RefPtr<ComputedStyle> div5_style = ComputedStyle::create();
  div5_style->setHeight(Length(kHeight, Fixed));
  div5_style->setMarginTop(Length(kDiv5MarginTop, Fixed));
  NGBox* div5 = new NGBox(div5_style.get());

  // DIV7
  RefPtr<ComputedStyle> div7_style = ComputedStyle::create();
  div7_style->setMarginTop(Length(kDiv7MarginTop, Fixed));
  NGBox* div7 = new NGBox(div7_style.get());

  div1->SetFirstChild(div2);
  div2->SetNextSibling(div3);
  div1->SetNextSibling(div4);
  div4->SetNextSibling(div5);
  div5->SetFirstChild(div6);
  div6->SetNextSibling(div7);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, div1);

  ASSERT_EQ(frag->Children().size(), 3UL);

  // DIV1
  const NGPhysicalFragmentBase* child = frag->Children()[0];
  EXPECT_EQ(kHeight, child->Height());
  EXPECT_EQ(0, child->TopOffset());

  // DIV5
  child = frag->Children()[2];
  EXPECT_EQ(kHeight, child->Height());
  EXPECT_EQ(kHeight + kExpectedCollapsedMargin, child->TopOffset());
}

// Verifies the collapsing margins case for the next pair:
// - bottom margin of a last in-flow child and bottom margin of its parent if
//   the parent has 'auto' computed height
//
// Test case's HTML representation:
// <div style="margin-bottom: 20px; height: 50px;">            <!-- DIV1 -->
//   <div style="margin-bottom: 200px; height: 50px;"/>        <!-- DIV2 -->
// </div>
//
// Expected:
//   1) Margins are collapsed with the result = std::max(20, 200)
//      if DIV1.height == auto
//   2) Margins are NOT collapsed if DIV1.height != auto
TEST_F(NGBlockLayoutAlgorithmTest, CollapsingMarginsCase3) {
  const int kHeight = 50;
  const int kDiv1MarginBottom = 20;
  const int kDiv2MarginBottom = 200;

  // DIV1
  RefPtr<ComputedStyle> div1_style = ComputedStyle::create();
  div1_style->setMarginBottom(Length(kDiv1MarginBottom, Fixed));
  NGBox* div1 = new NGBox(div1_style.get());

  // DIV2
  RefPtr<ComputedStyle> div2_style = ComputedStyle::create();
  div2_style->setHeight(Length(kHeight, Fixed));
  div2_style->setMarginBottom(Length(kDiv2MarginBottom, Fixed));
  NGBox* div2 = new NGBox(div2_style.get());

  div1->SetFirstChild(div2);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, div1);

  // Verify that margins are collapsed.
  EXPECT_EQ(NGMarginStrut({LayoutUnit(0), LayoutUnit(kDiv2MarginBottom)}),
            frag->MarginStrut());

  // Verify that margins are NOT collapsed.
  div1_style->setHeight(Length(kHeight, Fixed));
  frag = RunBlockLayoutAlgorithm(space, div1);
  EXPECT_EQ(NGMarginStrut({LayoutUnit(0), LayoutUnit(kDiv1MarginBottom)}),
            frag->MarginStrut());
}

// Verifies that 2 adjoining margins are not collapsed if there is padding or
// border that separates them.
//
// Test case's HTML representation:
// <div style="margin: 30px 0px; padding: 20px 0px;">    <!-- DIV1 -->
//   <div style="margin: 200px 0px; height: 50px;"/>     <!-- DIV2 -->
// </div>
//
// Expected:
// Margins do NOT collapse if there is an interfering padding or border.
TEST_F(NGBlockLayoutAlgorithmTest, CollapsingMarginsCase4) {
  const int kHeight = 50;
  const int kDiv1Margin = 30;
  const int kDiv1Padding = 20;
  const int kDiv2Margin = 200;

  // DIV1
  RefPtr<ComputedStyle> div1_style = ComputedStyle::create();
  div1_style->setMarginTop(Length(kDiv1Margin, Fixed));
  div1_style->setMarginBottom(Length(kDiv1Margin, Fixed));
  div1_style->setPaddingTop(Length(kDiv1Padding, Fixed));
  div1_style->setPaddingBottom(Length(kDiv1Padding, Fixed));
  NGBox* div1 = new NGBox(div1_style.get());

  // DIV2
  RefPtr<ComputedStyle> div2_style = ComputedStyle::create();
  div2_style->setHeight(Length(kHeight, Fixed));
  div2_style->setMarginTop(Length(kDiv2Margin, Fixed));
  div2_style->setMarginBottom(Length(kDiv2Margin, Fixed));
  NGBox* div2 = new NGBox(div2_style.get());

  div1->SetFirstChild(div2);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, div1);

  // Verify that margins do NOT collapse.
  frag = RunBlockLayoutAlgorithm(space, div1);
  EXPECT_EQ(NGMarginStrut({LayoutUnit(kDiv1Margin), LayoutUnit(kDiv1Margin)}),
            frag->MarginStrut());
  ASSERT_EQ(frag->Children().size(), 1UL);

  EXPECT_EQ(NGMarginStrut({LayoutUnit(kDiv2Margin), LayoutUnit(kDiv2Margin)}),
            frag->Children()[0]->MarginStrut());

  // Reset padding and verify that margins DO collapse.
  div1_style->setPaddingTop(Length(0, Fixed));
  div1_style->setPaddingBottom(Length(0, Fixed));
  frag = RunBlockLayoutAlgorithm(space, div1);
  EXPECT_EQ(NGMarginStrut({LayoutUnit(kDiv2Margin), LayoutUnit(kDiv2Margin)}),
            frag->MarginStrut());
}

// Verifies that margins of 2 adjoining blocks with different writing modes
// get collapsed.
//
// Test case's HTML representation:
//   <div style="writing-mode: vertical-lr;">
//     <div style="margin-right: 60px; width: 60px;">vertical</div>
//     <div style="margin-left: 100px; writing-mode: horizontal-tb;">
//       horizontal
//     </div>
//   </div>
TEST_F(NGBlockLayoutAlgorithmTest, CollapsingMarginsCase5) {
  const int kVerticalDivMarginRight = 60;
  const int kVerticalDivWidth = 60;
  const int kHorizontalDivMarginLeft = 100;

  style_->setWidth(Length(500, Fixed));
  style_->setHeight(Length(500, Fixed));
  style_->setWritingMode(LeftToRightWritingMode);

  // Vertical DIV
  RefPtr<ComputedStyle> vertical_style = ComputedStyle::create();
  vertical_style->setMarginRight(Length(kVerticalDivMarginRight, Fixed));
  vertical_style->setWidth(Length(kVerticalDivWidth, Fixed));
  NGBox* vertical_div = new NGBox(vertical_style.get());

  // Horizontal DIV
  RefPtr<ComputedStyle> horizontal_style = ComputedStyle::create();
  horizontal_style->setMarginLeft(Length(kHorizontalDivMarginLeft, Fixed));
  horizontal_style->setWritingMode(TopToBottomWritingMode);
  NGBox* horizontal_div = new NGBox(horizontal_style.get());

  vertical_div->SetNextSibling(horizontal_div);

  auto* space =
      new NGConstraintSpace(VerticalLeftRight, LeftToRight,
                            NGLogicalSize(LayoutUnit(500), LayoutUnit(500)));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, vertical_div);

  ASSERT_EQ(frag->Children().size(), 2UL);
  const NGPhysicalFragmentBase* child = frag->Children()[1];
  // Horizontal div
  EXPECT_EQ(0, child->TopOffset());
  EXPECT_EQ(kVerticalDivWidth + kHorizontalDivMarginLeft, child->LeftOffset());
}

// Verifies that a box's size includes its borders and padding, and that
// children are positioned inside the content box.
//
// Test case's HTML representation:
// <style>
//   #div1 { width:100px; height:100px; }
//   #div1 { border-style:solid; border-width:1px 2px 3px 4px; }
//   #div1 { padding:5px 6px 7px 8px; }
// </style>
// <div id="div1">
//    <div id="div2"></div>
// </div>
TEST_F(NGBlockLayoutAlgorithmTest, BorderAndPadding) {
  const int kWidth = 100;
  const int kHeight = 100;
  const int kBorderTop = 1;
  const int kBorderRight = 2;
  const int kBorderBottom = 3;
  const int kBorderLeft = 4;
  const int kPaddingTop = 5;
  const int kPaddingRight = 6;
  const int kPaddingBottom = 7;
  const int kPaddingLeft = 8;
  RefPtr<ComputedStyle> div1_style = ComputedStyle::create();

  div1_style->setWidth(Length(kWidth, Fixed));
  div1_style->setHeight(Length(kHeight, Fixed));

  div1_style->setBorderTopWidth(kBorderTop);
  div1_style->setBorderTopStyle(BorderStyleSolid);
  div1_style->setBorderRightWidth(kBorderRight);
  div1_style->setBorderRightStyle(BorderStyleSolid);
  div1_style->setBorderBottomWidth(kBorderBottom);
  div1_style->setBorderBottomStyle(BorderStyleSolid);
  div1_style->setBorderLeftWidth(kBorderLeft);
  div1_style->setBorderLeftStyle(BorderStyleSolid);

  div1_style->setPaddingTop(Length(kPaddingTop, Fixed));
  div1_style->setPaddingRight(Length(kPaddingRight, Fixed));
  div1_style->setPaddingBottom(Length(kPaddingBottom, Fixed));
  div1_style->setPaddingLeft(Length(kPaddingLeft, Fixed));
  NGBox* div1 = new NGBox(div1_style.get());

  RefPtr<ComputedStyle> div2_style = ComputedStyle::create();
  NGBox* div2 = new NGBox(div2_style.get());

  div1->SetFirstChild(div2);

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(1000), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, div1);

  ASSERT_EQ(frag->Children().size(), 1UL);

  // div1
  const NGPhysicalFragmentBase* child = frag->Children()[0];
  EXPECT_EQ(kBorderLeft + kPaddingLeft + kWidth + kPaddingRight + kBorderRight,
            child->Width());
  EXPECT_EQ(kBorderTop + kPaddingTop + kHeight + kPaddingBottom + kBorderBottom,
            child->Height());

  ASSERT_TRUE(child->Type() == NGPhysicalFragmentBase::FragmentBox);
  ASSERT_EQ(static_cast<const NGPhysicalFragment*>(child)->Children().size(),
            1UL);

  // div2
  child = static_cast<const NGPhysicalFragment*>(child)->Children()[0];
  EXPECT_EQ(kBorderTop + kPaddingTop, child->TopOffset());
  EXPECT_EQ(kBorderLeft + kPaddingLeft, child->LeftOffset());
}

TEST_F(NGBlockLayoutAlgorithmTest, PercentageSize) {
  const int kPaddingLeft = 10;
  const int kWidth = 30;
  style_->setWidth(Length(kWidth, Fixed));
  style_->setPaddingLeft(Length(kPaddingLeft, Fixed));

  RefPtr<ComputedStyle> first_style = ComputedStyle::create();
  first_style->setWidth(Length(40, Percent));
  NGBox* first_child = new NGBox(first_style.get());

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, first_child);

  EXPECT_EQ(frag->Width(), LayoutUnit(kWidth + kPaddingLeft));
  EXPECT_EQ(frag->Type(), NGPhysicalFragmentBase::FragmentBox);
  ASSERT_EQ(frag->Children().size(), 1UL);

  const NGPhysicalFragmentBase* child = frag->Children()[0];
  EXPECT_EQ(child->Width(), LayoutUnit(12));
}

// A very simple auto margin case. We rely on the tests in ng_length_utils_test
// for the more complex cases; just make sure we handle auto at all here.
TEST_F(NGBlockLayoutAlgorithmTest, AutoMargin) {
  const int kPaddingLeft = 10;
  const int kWidth = 30;
  style_->setWidth(Length(kWidth, Fixed));
  style_->setPaddingLeft(Length(kPaddingLeft, Fixed));

  RefPtr<ComputedStyle> first_style = ComputedStyle::create();
  const int kChildWidth = 10;
  first_style->setWidth(Length(kChildWidth, Fixed));
  first_style->setMarginLeft(Length(Auto));
  first_style->setMarginRight(Length(Auto));
  NGBox* first_child = new NGBox(first_style.get());

  auto* space =
      new NGConstraintSpace(HorizontalTopBottom, LeftToRight,
                            NGLogicalSize(LayoutUnit(100), NGSizeIndefinite));
  NGPhysicalFragment* frag = RunBlockLayoutAlgorithm(space, first_child);

  EXPECT_EQ(LayoutUnit(kWidth + kPaddingLeft), frag->Width());
  EXPECT_EQ(NGPhysicalFragmentBase::FragmentBox, frag->Type());
  EXPECT_EQ(LayoutUnit(kWidth + kPaddingLeft), frag->WidthOverflow());
  ASSERT_EQ(1UL, frag->Children().size());

  const NGPhysicalFragmentBase* child = frag->Children()[0];
  EXPECT_EQ(LayoutUnit(kChildWidth), child->Width());
  EXPECT_EQ(LayoutUnit(kPaddingLeft + 10), child->LeftOffset());
  EXPECT_EQ(LayoutUnit(0), child->TopOffset());
}
}  // namespace
}  // namespace blink
