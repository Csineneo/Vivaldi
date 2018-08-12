// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_constraint_space.h"

#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_layout_opportunity_iterator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

RefPtr<NGConstraintSpace> ConstructConstraintSpace(
    NGWritingMode writing_mode,
    TextDirection direction,
    NGLogicalSize size,
    const NGLogicalOffset& bfc_offset = {}) {
  return NGConstraintSpaceBuilder(writing_mode)
      .SetTextDirection(direction)
      .SetAvailableSize(size)
      .SetPercentageResolutionSize(size)
      .SetIsFixedSizeInline(true)
      .SetIsInlineDirectionTriggersScrollbar(true)
      .SetFragmentationType(NGFragmentationType::kFragmentColumn)
      .SetBfcOffset(bfc_offset)
      .ToConstraintSpace(writing_mode);
}

TEST(NGConstraintSpaceTest, LayoutOpportunitiesNoExclusions) {
  NGLogicalSize size;
  size.inline_size = LayoutUnit(600);
  size.block_size = LayoutUnit(400);
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr, size);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize());
  // 600x400 at (0,0)
  NGLayoutOpportunity opp1 = {{}, {LayoutUnit(600), LayoutUnit(400)}};
  EXPECT_EQ(opp1, iterator.Next());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

TEST(NGConstraintSpaceTest, LayoutOpportunitiesTopRightExclusion) {
  NGLogicalSize size;
  size.inline_size = LayoutUnit(600);
  size.block_size = LayoutUnit(400);
  // Create a space with a 100x100 exclusion in the top right corner.
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr, size);
  NGExclusion exclusion;
  exclusion.rect.size = {LayoutUnit(100), LayoutUnit(100)};
  exclusion.rect.offset = {LayoutUnit(500), LayoutUnit()};
  space->AddExclusion(exclusion);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize());

  // First opportunity should be to the left of the exclusion: 500x400 at (0,0)
  NGLayoutOpportunity opp1 = {{}, {LayoutUnit(500), LayoutUnit(400)}};
  EXPECT_EQ(opp1, iterator.Next());

  // Second opportunity should be below the exclusion: 600x300 at (0,100)
  NGLayoutOpportunity opp2 = {{LayoutUnit(), LayoutUnit(100)},
                              {LayoutUnit(600), LayoutUnit(300)}};
  EXPECT_EQ(opp2, iterator.Next());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

TEST(NGConstraintSpaceTest, LayoutOpportunitiesTopLeftExclusion) {
  NGLogicalSize size;
  size.inline_size = LayoutUnit(600);
  size.block_size = LayoutUnit(400);
  // Create a space with a 100x100 exclusion in the top left corner.
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr, size);
  NGExclusion exclusion;
  exclusion.rect.size = {LayoutUnit(100), LayoutUnit(100)};
  space->AddExclusion(exclusion);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize());
  // First opportunity should be to the right of the exclusion:
  // 500x400 at (100, 0)
  NGLayoutOpportunity opp1 = {{LayoutUnit(100), LayoutUnit()},
                              {LayoutUnit(500), LayoutUnit(400)}};
  EXPECT_EQ(opp1, iterator.Next());

  // Second opportunity should be below the exclusion: 600x300 at (0,100)
  NGLayoutOpportunity opp2 = {{LayoutUnit(), LayoutUnit(100)},
                              {LayoutUnit(600), LayoutUnit(300)}};
  EXPECT_EQ(opp2, iterator.Next());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

// Verifies that Layout Opportunity iterator produces 7 layout opportunities
// from 4 start points created by 2 CSS exclusions positioned in the middle of
// the main constraint space.
//
// Test case visual representation:
//
//         100  200   300  400  500
//     (1)--|----|-(2)-|----|----|-(3)-+
//  50 |                               |
// 100 |                               |
// 150 |                               |
// 200 |       ******                  |
// 250 |       ******                  |
// 300 (4)                             |
// 350 |                         ***   |
//     +-------------------------------+
//
// Expected:
//   Layout opportunity iterator generates the next opportunities:
//   - 1st Start Point: 0,0 600x200; 0,0 150x400
//   - 2nd Start Point: 250,0 350x350; 250,0 250x400
//   - 3rd Start Point: 550,0 50x400
//   - 4th Start Point: 0,300 600x50; 0,300 500x100
TEST(NGConstraintSpaceTest, LayoutOpportunitiesTwoInMiddle) {
  NGLogicalSize size;
  size.inline_size = LayoutUnit(600);
  size.block_size = LayoutUnit(400);
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr, size);
  // Add exclusions
  NGExclusion exclusion1;
  exclusion1.rect.size = {LayoutUnit(100), LayoutUnit(100)};
  exclusion1.rect.offset = {LayoutUnit(150), LayoutUnit(200)};
  space->AddExclusion(exclusion1);
  NGExclusion exclusion2;
  exclusion2.rect.size = {LayoutUnit(50), LayoutUnit(50)};
  exclusion2.rect.offset = {LayoutUnit(500), LayoutUnit(350)};
  space->AddExclusion(exclusion2);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize());
  NGLogicalOffset start_point1;
  // 600x200 at (0,0)
  NGLayoutOpportunity opp1 = {start_point1, {LayoutUnit(600), LayoutUnit(200)}};
  EXPECT_EQ(opp1, (iterator.Next()));
  // 150x400 at (0,0)
  NGLayoutOpportunity opp2 = {start_point1, {LayoutUnit(150), LayoutUnit(400)}};
  EXPECT_EQ(opp2, (iterator.Next()));

  NGLogicalOffset start_point2 = {LayoutUnit(250), LayoutUnit()};
  // 350x350 at (250,0)
  NGLayoutOpportunity opp3 = {start_point2, {LayoutUnit(350), LayoutUnit(350)}};
  EXPECT_EQ(opp3, (iterator.Next()));
  // 250x400 at (250,0)
  NGLayoutOpportunity opp4 = {start_point2, {LayoutUnit(250), LayoutUnit(400)}};
  EXPECT_EQ(opp4, (iterator.Next()));

  NGLogicalOffset start_point3 = {LayoutUnit(550), LayoutUnit()};
  // 50x400 at (550,0)
  NGLayoutOpportunity opp5 = {start_point3, {LayoutUnit(50), LayoutUnit(400)}};
  EXPECT_EQ(opp5, (iterator.Next()));

  NGLogicalOffset start_point4 = {LayoutUnit(), LayoutUnit(300)};
  // 600x50 at (0,300)
  NGLayoutOpportunity opp6 = {start_point4, {LayoutUnit(600), LayoutUnit(50)}};
  EXPECT_EQ(opp6, (iterator.Next()));
  // 500x100 at (0,300)
  NGLayoutOpportunity opp7 = {start_point4, {LayoutUnit(500), LayoutUnit(100)}};
  EXPECT_EQ(opp7, (iterator.Next()));

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

// This test is the same as LayoutOpportunitiesTwoInMiddle with the only
// difference that NGLayoutOpportunityIterator takes 2 additional arguments:
// - origin_point that changes the iterator to return Layout Opportunities that
// lay after the origin point.
// - leader_point that together with origin_point creates a temporary exclusion
//
// Expected:
//   Layout opportunity iterator generates the next opportunities:
//   - 1st Start Point (0, 200): 350x150, 250x400
//   - 3rd Start Point (550, 200): 50x400
//   - 4th Start Point (0, 300): 600x50, 500x300
//   - 5th Start Point (0, 400): 600x200
//   All other opportunities that are located before the origin point should be
//   filtered out.
TEST(NGConstraintSpaceTest, LayoutOpportunitiesTwoInMiddleWithOriginAndLeader) {
  NGLogicalSize size;
  size.inline_size = LayoutUnit(600);
  size.block_size = LayoutUnit(400);
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr, size);
  // Add exclusions
  NGExclusion exclusion1;
  exclusion1.rect.size = {LayoutUnit(100), LayoutUnit(100)};
  exclusion1.rect.offset = {LayoutUnit(150), LayoutUnit(200)};
  space->AddExclusion(exclusion1);
  NGExclusion exclusion2;
  exclusion2.rect.size = {LayoutUnit(50), LayoutUnit(50)};
  exclusion2.rect.offset = {LayoutUnit(500), LayoutUnit(350)};
  space->AddExclusion(exclusion2);

  const NGLogicalOffset origin_point = {LayoutUnit(), LayoutUnit(200)};
  const NGLogicalOffset leader_point = {LayoutUnit(250), LayoutUnit(300)};
  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize(),
                                       origin_point, leader_point);

  NGLogicalOffset start_point1 = {LayoutUnit(250), LayoutUnit(200)};
  // 350x150 at (250,200)
  NGLayoutOpportunity opp1 = {start_point1, {LayoutUnit(350), LayoutUnit(150)}};
  EXPECT_EQ(opp1, iterator.Next());
  // 250x400 at (250,200)
  NGLayoutOpportunity opp2 = {start_point1, {LayoutUnit(250), LayoutUnit(400)}};
  EXPECT_EQ(opp2, iterator.Next());

  NGLogicalOffset start_point2 = {LayoutUnit(550), LayoutUnit(200)};
  // 50x400 at (550,200)
  NGLayoutOpportunity opp3 = {start_point2, {LayoutUnit(50), LayoutUnit(400)}};
  EXPECT_EQ(opp3, iterator.Next());

  NGLogicalOffset start_point3 = {LayoutUnit(), LayoutUnit(300)};
  // 600x50 at (0,300)
  NGLayoutOpportunity opp4 = {start_point3, {LayoutUnit(600), LayoutUnit(50)}};
  EXPECT_EQ(opp4, iterator.Next());
  // 500x300 at (0,300)
  NGLayoutOpportunity opp5 = {start_point3, {LayoutUnit(500), LayoutUnit(300)}};
  EXPECT_EQ(opp5, iterator.Next());

  // 4th Start Point
  NGLogicalOffset start_point4 = {LayoutUnit(), LayoutUnit(400)};
  // 600x200 at (0,400)
  NGLayoutOpportunity opp6 = {start_point4, {LayoutUnit(600), LayoutUnit(200)}};
  EXPECT_EQ(opp6, iterator.Next());

  // TODO(glebl): The opportunity below should not be generated.
  EXPECT_EQ("350x200 at (250,400)", iterator.Next().ToString());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

// Verifies that Layout Opportunity iterator ignores the exclusion that is not
// within constraint space.
//
// Test case visual representation:
//
//         100  200  300  400  500
//     +----|----|----|----|----|----+
//  50 |                             |
// 100 |                             |
//     +-----------------------------+
//      ***  <- Exclusion
//
// Expected:
//   Layout opportunity iterator generates only one opportunity that equals to
//   available constraint space, i.e. 0,0 600x200
TEST(NGConstraintSpaceTest, LayoutOpportunitiesWithOutOfBoundsExclusions) {
  NGLogicalSize size = {LayoutUnit(600), LayoutUnit(100)};
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr, size);
  NGExclusion exclusion;
  exclusion.rect.size = {LayoutUnit(100), LayoutUnit(100)};
  exclusion.rect.offset = {LayoutUnit(), LayoutUnit(150)};
  space->AddExclusion(exclusion);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize());
  // 600x100 at (0,0)
  NGLayoutOpportunity opp = {{}, size};
  EXPECT_EQ(opp, iterator.Next());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

// Verifies that we combine 2 adjoining left exclusions into one left exclusion.
TEST(NGConstraintSpaceTest, TwoLeftExclusionsShadowEachOther) {
  NGLogicalOffset bfc_offset = {LayoutUnit(8), LayoutUnit(8)};
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr,
                               {LayoutUnit(200), LayoutUnit(200)}, bfc_offset);

  NGExclusion small_left;
  small_left.rect.size = {LayoutUnit(10), LayoutUnit(10)};
  small_left.rect.offset = bfc_offset;
  small_left.type = NGExclusion::kFloatLeft;
  space->AddExclusion(small_left);

  NGExclusion big_left;
  big_left.rect.size = {LayoutUnit(20), LayoutUnit(20)};
  big_left.rect.offset = bfc_offset;
  big_left.rect.offset.inline_offset += small_left.rect.InlineSize();
  big_left.type = NGExclusion::kFloatLeft;
  space->AddExclusion(big_left);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize(),
                                       bfc_offset);

  NGLogicalOffset start_point1 = bfc_offset;
  start_point1.inline_offset +=
      small_left.rect.InlineSize() + big_left.rect.InlineSize();
  // 170x200 at (38, 8)
  NGLayoutOpportunity opportunity1 = {start_point1,
                                      {LayoutUnit(170), LayoutUnit(200)}};
  EXPECT_EQ(opportunity1, iterator.Next());

  NGLogicalOffset start_point2 = bfc_offset;
  start_point2.block_offset += big_left.rect.BlockSize();
  // 200x180 at (8, 28)
  NGLayoutOpportunity opportunity2 = {start_point2,
                                      {LayoutUnit(200), LayoutUnit(180)}};
  EXPECT_EQ(opportunity2, iterator.Next());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

// Verifies that we combine 2 adjoining right exclusions into one right
// exclusion.
TEST(NGConstraintSpaceTest, TwoRightExclusionsShadowEachOther) {
  NGLogicalOffset bfc_offset = {LayoutUnit(8), LayoutUnit(8)};
  RefPtr<NGConstraintSpace> space =
      ConstructConstraintSpace(kHorizontalTopBottom, TextDirection::kLtr,
                               {LayoutUnit(200), LayoutUnit(200)}, bfc_offset);

  NGExclusion small_right;
  small_right.rect.size = {LayoutUnit(10), LayoutUnit(10)};
  small_right.rect.offset = bfc_offset;
  small_right.rect.offset.inline_offset +=
      space->AvailableSize().inline_size - small_right.rect.InlineSize();
  small_right.type = NGExclusion::kFloatRight;
  space->AddExclusion(small_right);

  NGExclusion big_right;
  big_right.rect.size = {LayoutUnit(20), LayoutUnit(20)};
  big_right.rect.offset = bfc_offset;
  big_right.rect.offset.inline_offset += space->AvailableSize().inline_size -
                                         small_right.rect.InlineSize() -
                                         big_right.rect.InlineSize();
  big_right.type = NGExclusion::kFloatRight;
  space->AddExclusion(big_right);

  NGLayoutOpportunityIterator iterator(space.Get(), space->AvailableSize(),
                                       bfc_offset);

  NGLogicalOffset start_point1 = bfc_offset;
  // 170x200 at (8, 8)
  NGLayoutOpportunity opportunity1 = {start_point1,
                                      {LayoutUnit(170), LayoutUnit(200)}};
  EXPECT_EQ(opportunity1, iterator.Next());

  NGLogicalOffset start_point2 = bfc_offset;
  start_point2.block_offset += big_right.rect.BlockSize();
  // 200x180 at (8, 28)
  NGLayoutOpportunity opportunity2 = {start_point2,
                                      {LayoutUnit(200), LayoutUnit(180)}};
  EXPECT_EQ(opportunity2, iterator.Next());

  EXPECT_EQ(NGLayoutOpportunity(), iterator.Next());
}

}  // namespace
}  // namespace blink
