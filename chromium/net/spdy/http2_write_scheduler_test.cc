// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/http2_write_scheduler.h"

#include "net/spdy/spdy_test_utils.h"
#include "net/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

namespace test {

template <typename StreamIdType>
class Http2PriorityWriteSchedulerPeer {
 public:
  explicit Http2PriorityWriteSchedulerPeer(
      Http2PriorityWriteScheduler<StreamIdType>* scheduler)
      : scheduler_(scheduler) {}

  int TotalChildWeights(StreamIdType stream_id) const {
    return scheduler_->FindStream(stream_id)->total_child_weights;
  }

  bool ValidateInvariants() const {
    return scheduler_->ValidateInvariantsForTests();
  }

 private:
  Http2PriorityWriteScheduler<StreamIdType>* scheduler_;
};

class Http2PriorityWriteSchedulerTest : public ::testing::Test {
 protected:
  using SpdyStreamId = uint32_t;

  Http2PriorityWriteSchedulerTest() : peer_(&scheduler_) {}

  Http2PriorityWriteScheduler<SpdyStreamId> scheduler_;
  Http2PriorityWriteSchedulerPeer<SpdyStreamId> peer_;
};

TEST_F(Http2PriorityWriteSchedulerTest, RegisterAndUnregisterStreams) {
  EXPECT_EQ(1, scheduler_.num_streams());
  EXPECT_TRUE(scheduler_.StreamRegistered(0));
  EXPECT_FALSE(scheduler_.StreamRegistered(1));

  scheduler_.RegisterStream(1, 0, 100, false);
  EXPECT_EQ(2, scheduler_.num_streams());
  ASSERT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_EQ(100, scheduler_.GetStreamWeight(1));
  EXPECT_FALSE(scheduler_.StreamRegistered(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));

  scheduler_.RegisterStream(5, 0, 50, false);
  // Should not be able to add a stream with an id that already exists.
  EXPECT_SPDY_BUG(scheduler_.RegisterStream(5, 1, 50, false),
                  "Stream 5 already registered");
  EXPECT_EQ(3, scheduler_.num_streams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  ASSERT_TRUE(scheduler_.StreamRegistered(5));
  EXPECT_EQ(50, scheduler_.GetStreamWeight(5));
  EXPECT_FALSE(scheduler_.StreamRegistered(13));

  scheduler_.RegisterStream(13, 5, 130, true);
  EXPECT_EQ(4, scheduler_.num_streams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_TRUE(scheduler_.StreamRegistered(5));
  ASSERT_TRUE(scheduler_.StreamRegistered(13));
  EXPECT_EQ(130, scheduler_.GetStreamWeight(13));
  EXPECT_EQ(5u, scheduler_.GetStreamParent(13));

  scheduler_.UnregisterStream(5);
  // Cannot remove a stream that has already been removed.
  EXPECT_SPDY_BUG(scheduler_.UnregisterStream(5), "Stream 5 not registered");
  EXPECT_EQ(3, scheduler_.num_streams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_FALSE(scheduler_.StreamRegistered(5));
  EXPECT_TRUE(scheduler_.StreamRegistered(13));
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamParent(13));

  // The parent stream 19 doesn't exist, so this should use 0 as parent stream:
  EXPECT_SPDY_BUG(scheduler_.RegisterStream(7, 19, 70, false),
                  "Parent stream 19 not registered");
  EXPECT_TRUE(scheduler_.StreamRegistered(7));
  EXPECT_EQ(0u, scheduler_.GetStreamParent(7));
  // Now stream 7 already exists, so this should fail:
  EXPECT_SPDY_BUG(scheduler_.RegisterStream(7, 1, 70, false),
                  "Stream 7 already registered");
  // Try adding a second child to stream 13:
  scheduler_.RegisterStream(17, 13, 170, false);

  // TODO(birenroy): Add a separate test that verifies weight invariants when
  // UpdateStreamWeight is called.
  scheduler_.UpdateStreamWeight(17, 150);
  EXPECT_EQ(150, scheduler_.GetStreamWeight(17));

  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, RegisterStream) {
  EXPECT_FALSE(scheduler_.StreamRegistered(1));
  scheduler_.RegisterStream(1, 3);
  EXPECT_EQ(0u, scheduler_.NumReadyStreams());
  EXPECT_TRUE(scheduler_.StreamRegistered(1));
  EXPECT_EQ(3, scheduler_.GetStreamPriority(1));
  EXPECT_EQ(SpdyPriorityToHttp2Weight(3), scheduler_.GetStreamWeight(1));
  EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamParent(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());

  EXPECT_SPDY_BUG(scheduler_.RegisterStream(1, 4),
                  "Stream 1 already registered");
  EXPECT_EQ(3, scheduler_.GetStreamPriority(1));
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamWeight) {
  EXPECT_SPDY_BUG(
      EXPECT_EQ(kHttp2MinStreamWeight, scheduler_.GetStreamWeight(3)),
      "Stream 3 not registered");
  scheduler_.RegisterStream(3, 0, 130, true);
  EXPECT_EQ(130, scheduler_.GetStreamWeight(3));
  scheduler_.UpdateStreamWeight(3, 50);
  EXPECT_EQ(50, scheduler_.GetStreamWeight(3));
  scheduler_.UnregisterStream(3);
  EXPECT_SPDY_BUG(
      EXPECT_EQ(kHttp2MinStreamWeight, scheduler_.GetStreamWeight(3)),
      "Stream 3 not registered");
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamPriority) {
  EXPECT_SPDY_BUG(EXPECT_EQ(kV3LowestPriority, scheduler_.GetStreamPriority(3)),
                  "Stream 3 not registered");
  scheduler_.RegisterStream(3, 0, 130, true);
  EXPECT_EQ(Http2WeightToSpdyPriority(130), scheduler_.GetStreamPriority(3));
  scheduler_.UpdateStreamWeight(3, 50);
  EXPECT_EQ(Http2WeightToSpdyPriority(50), scheduler_.GetStreamPriority(3));
  scheduler_.UnregisterStream(3);
  EXPECT_SPDY_BUG(EXPECT_EQ(kV3LowestPriority, scheduler_.GetStreamPriority(3)),
                  "Stream 3 not registered");
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamParent) {
  EXPECT_SPDY_BUG(EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamParent(3)),
                  "Stream 3 not registered");
  scheduler_.RegisterStream(2, 0, 20, false);
  scheduler_.RegisterStream(3, 2, 30, false);
  EXPECT_EQ(2u, scheduler_.GetStreamParent(3));
  scheduler_.UnregisterStream(3);
  EXPECT_SPDY_BUG(EXPECT_EQ(kHttp2RootStreamId, scheduler_.GetStreamParent(3)),
                  "Stream 3 not registered");
}

TEST_F(Http2PriorityWriteSchedulerTest, GetStreamChildren) {
  EXPECT_SPDY_BUG(EXPECT_THAT(scheduler_.GetStreamChildren(7), IsEmpty()),
                  "Stream 7 not registered");
  scheduler_.RegisterStream(7, 0, 70, false);
  EXPECT_THAT(scheduler_.GetStreamChildren(7), IsEmpty());
  scheduler_.RegisterStream(9, 7, 90, false);
  scheduler_.RegisterStream(15, 7, 150, false);
  EXPECT_THAT(scheduler_.GetStreamChildren(7), UnorderedElementsAre(9, 15));
  scheduler_.UnregisterStream(7);
  EXPECT_SPDY_BUG(EXPECT_THAT(scheduler_.GetStreamChildren(7), IsEmpty()),
                  "Stream 7 not registered");
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamWeight) {
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamWeight(0, 10),
                  "Cannot set weight of root stream");
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamWeight(3, 10),
                  "Stream 3 not registered");
  scheduler_.RegisterStream(3, 0, 10, false);
  scheduler_.UpdateStreamWeight(3, 20);
  EXPECT_EQ(20, scheduler_.GetStreamWeight(3));
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamWeight(3, 500), "Invalid weight: 500");
  EXPECT_EQ(kHttp2MaxStreamWeight, scheduler_.GetStreamWeight(3));
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamWeight(3, 0), "Invalid weight: 0");
  EXPECT_EQ(kHttp2MinStreamWeight, scheduler_.GetStreamWeight(3));
  scheduler_.UnregisterStream(3);
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamWeight(3, 10),
                  "Stream 3 not registered");
}

// Basic case of reparenting a subtree.
TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentBasicNonExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \
    3   4
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 0, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 1, 100, false);
  scheduler_.UpdateStreamParent(1, 2, false);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

// Basic case of reparenting a subtree.  Result here is the same as the
// non-exclusive case.
TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentBasicExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \
    3   4
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 0, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 1, 100, false);
  scheduler_.UpdateStreamParent(1, 2, true);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

// We can't set the parent of a nonexistent stream, or set the parent to a
// nonexistent stream.
TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentNonexistent) {
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 0, 100, false);
  for (bool exclusive : {true, false}) {
    EXPECT_SPDY_BUG(scheduler_.UpdateStreamParent(1, 3, exclusive),
                    "Parent stream 3 not registered");
    EXPECT_SPDY_BUG(scheduler_.UpdateStreamParent(4, 2, exclusive),
                    "Stream 4 not registered");
    EXPECT_SPDY_BUG(scheduler_.UpdateStreamParent(3, 4, exclusive),
                    "Stream 3 not registered");
    EXPECT_THAT(scheduler_.GetStreamChildren(0), UnorderedElementsAre(1, 2));
    EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());
    EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
  }
  ASSERT_TRUE(peer_.ValidateInvariants());
}

// We should be able to add multiple children to streams.
TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentMultipleChildrenNonExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \   \
    3   4   5
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 0, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 1, 100, false);
  scheduler_.RegisterStream(5, 2, 100, false);
  scheduler_.UpdateStreamParent(2, 1, false);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentMultipleChildrenExclusive) {
  /* Tree:
        0
       / \
      1   2
     / \   \
    3   4   5
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 0, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 1, 100, false);
  scheduler_.RegisterStream(5, 2, 100, false);
  scheduler_.UpdateStreamParent(2, 1, true);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), UnorderedElementsAre(3, 4, 5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToChildNonExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
      |
      4
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 1, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 2, 100, false);
  scheduler_.UpdateStreamParent(1, 2, false);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), ElementsAre(3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), UnorderedElementsAre(1, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToChildExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
      |
      4
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 1, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 2, 100, false);
  scheduler_.UpdateStreamParent(1, 2, true);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(2));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(3, 4));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentToGrandchildNonExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
     / \
    4   5
    |
    6
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 1, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 2, 100, false);
  scheduler_.RegisterStream(5, 2, 100, false);
  scheduler_.RegisterStream(6, 4, 100, false);
  scheduler_.UpdateStreamParent(1, 4, false);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(4));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), UnorderedElementsAre(1, 6));
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(6), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest,
       UpdateStreamParentToGrandchildExclusive) {
  /* Tree:
        0
        |
        1
       / \
      2   3
     / \
    4   5
    |
    6
   */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 1, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  scheduler_.RegisterStream(4, 2, 100, false);
  scheduler_.RegisterStream(5, 2, 100, false);
  scheduler_.RegisterStream(6, 4, 100, false);
  scheduler_.UpdateStreamParent(1, 4, true);
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(4));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3, 6));
  EXPECT_THAT(scheduler_.GetStreamChildren(2), ElementsAre(5));
  EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(4), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(5), IsEmpty());
  EXPECT_THAT(scheduler_.GetStreamChildren(6), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToParent) {
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 1, 100, false);
  scheduler_.RegisterStream(3, 1, 100, false);
  for (bool exclusive : {true, false}) {
    scheduler_.UpdateStreamParent(2, 1, exclusive);
    EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
    EXPECT_THAT(scheduler_.GetStreamChildren(1), UnorderedElementsAre(2, 3));
    EXPECT_THAT(scheduler_.GetStreamChildren(2), IsEmpty());
    EXPECT_THAT(scheduler_.GetStreamChildren(3), IsEmpty());
  }
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, UpdateStreamParentToSelf) {
  scheduler_.RegisterStream(1, 0, 100, false);
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamParent(1, 1, false),
                  "Cannot set stream to be its own parent");
  EXPECT_SPDY_BUG(scheduler_.UpdateStreamParent(1, 1, true),
                  "Cannot set stream to be its own parent");
  EXPECT_THAT(scheduler_.GetStreamChildren(0), ElementsAre(1));
  EXPECT_THAT(scheduler_.GetStreamChildren(1), IsEmpty());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, StreamHasChild) {
  scheduler_.RegisterStream(1, 0, 10, false);
  scheduler_.RegisterStream(2, 1, 20, false);
  scheduler_.RegisterStream(3, 1, 30, false);
  EXPECT_SPDY_BUG(EXPECT_FALSE(scheduler_.StreamHasChild(4, 1)),
                  "Parent stream 4 not registered");
  EXPECT_SPDY_BUG(EXPECT_FALSE(scheduler_.StreamHasChild(3, 7)),
                  "Child stream 7 not registered");
  EXPECT_FALSE(scheduler_.StreamHasChild(3, 1));
  EXPECT_TRUE(scheduler_.StreamHasChild(1, 3));
  EXPECT_TRUE(scheduler_.StreamHasChild(1, 2));
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, BlockAndUnblock) {
  /* Create the tree.

             0
           / | \
          /  |  \
         1   2   3
        / \   \   \
       4   5   6   7
      /|  / \  |   |\
     8 9 10 11 12 13 14
    / \
   15 16

  */
  scheduler_.RegisterStream(1, 0, 100, false);
  scheduler_.RegisterStream(2, 0, 100, false);
  scheduler_.RegisterStream(3, 0, 100, false);
  scheduler_.RegisterStream(4, 1, 100, false);
  scheduler_.RegisterStream(5, 1, 100, false);
  scheduler_.RegisterStream(8, 4, 100, false);
  scheduler_.RegisterStream(9, 4, 100, false);
  scheduler_.RegisterStream(10, 5, 100, false);
  scheduler_.RegisterStream(11, 5, 100, false);
  scheduler_.RegisterStream(15, 8, 100, false);
  scheduler_.RegisterStream(16, 8, 100, false);
  scheduler_.RegisterStream(12, 2, 100, false);
  scheduler_.RegisterStream(6, 2, 100, true);
  scheduler_.RegisterStream(7, 0, 100, false);
  scheduler_.RegisterStream(13, 7, 100, true);
  scheduler_.RegisterStream(14, 7, 100, false);
  scheduler_.UpdateStreamParent(7, 3, false);
  EXPECT_EQ(0u, scheduler_.GetStreamParent(1));
  EXPECT_EQ(0u, scheduler_.GetStreamParent(2));
  EXPECT_EQ(0u, scheduler_.GetStreamParent(3));
  EXPECT_EQ(1u, scheduler_.GetStreamParent(4));
  EXPECT_EQ(1u, scheduler_.GetStreamParent(5));
  EXPECT_EQ(2u, scheduler_.GetStreamParent(6));
  EXPECT_EQ(3u, scheduler_.GetStreamParent(7));
  EXPECT_EQ(4u, scheduler_.GetStreamParent(8));
  EXPECT_EQ(4u, scheduler_.GetStreamParent(9));
  EXPECT_EQ(5u, scheduler_.GetStreamParent(10));
  EXPECT_EQ(5u, scheduler_.GetStreamParent(11));
  EXPECT_EQ(6u, scheduler_.GetStreamParent(12));
  EXPECT_EQ(7u, scheduler_.GetStreamParent(13));
  EXPECT_EQ(7u, scheduler_.GetStreamParent(14));
  EXPECT_EQ(8u, scheduler_.GetStreamParent(15));
  EXPECT_EQ(8u, scheduler_.GetStreamParent(16));
  ASSERT_TRUE(peer_.ValidateInvariants());

  EXPECT_EQ(peer_.TotalChildWeights(0), scheduler_.GetStreamWeight(1) +
                                            scheduler_.GetStreamWeight(2) +
                                            scheduler_.GetStreamWeight(3));
  EXPECT_EQ(peer_.TotalChildWeights(3), scheduler_.GetStreamWeight(7));
  EXPECT_EQ(peer_.TotalChildWeights(7),
            scheduler_.GetStreamWeight(13) + scheduler_.GetStreamWeight(14));
  EXPECT_EQ(peer_.TotalChildWeights(13), 0);
  EXPECT_EQ(peer_.TotalChildWeights(14), 0);

  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, HasReadyStreams) {
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  scheduler_.RegisterStream(1, 0, 10, false);
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  scheduler_.MarkStreamReady(1, false);
  EXPECT_TRUE(scheduler_.HasReadyStreams());
  scheduler_.MarkStreamNotReady(1);
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  scheduler_.MarkStreamReady(1, true);
  EXPECT_TRUE(scheduler_.HasReadyStreams());
  scheduler_.UnregisterStream(1);
  EXPECT_FALSE(scheduler_.HasReadyStreams());
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, CalculateRoundedWeights) {
  /* Create the tree.

           0
          / \
         1   2
       /| |\  |\
      8 3 4 5 6 7
  */
  scheduler_.RegisterStream(3, 0, 100, false);
  scheduler_.RegisterStream(4, 0, 100, false);
  scheduler_.RegisterStream(5, 0, 100, false);
  scheduler_.RegisterStream(1, 0, 10, true);
  scheduler_.RegisterStream(2, 0, 5, false);
  scheduler_.RegisterStream(6, 2, 1, false);
  scheduler_.RegisterStream(7, 2, 1, false);
  scheduler_.RegisterStream(8, 1, 1, false);

  // Remove higher-level streams.
  scheduler_.UnregisterStream(1);
  scheduler_.UnregisterStream(2);

  // 3.3 rounded down = 3.
  EXPECT_EQ(3, scheduler_.GetStreamWeight(3));
  EXPECT_EQ(3, scheduler_.GetStreamWeight(4));
  EXPECT_EQ(3, scheduler_.GetStreamWeight(5));
  // 2.5 rounded up = 3.
  EXPECT_EQ(3, scheduler_.GetStreamWeight(6));
  EXPECT_EQ(3, scheduler_.GetStreamWeight(7));
  // 0 is not a valid weight, so round up to 1.
  EXPECT_EQ(1, scheduler_.GetStreamWeight(8));
  ASSERT_TRUE(peer_.ValidateInvariants());
}

TEST_F(Http2PriorityWriteSchedulerTest, GetLatestEventWithPrecedence) {
  EXPECT_SPDY_BUG(scheduler_.RecordStreamEventTime(3, 5),
                  "Stream 3 not registered");
  EXPECT_SPDY_BUG(EXPECT_EQ(0, scheduler_.GetLatestEventWithPrecedence(4)),
                  "Stream 4 not registered");

  for (int i = 1; i < 5; ++i) {
    scheduler_.RegisterStream(i, 0, SpdyPriorityToHttp2Weight(i), false);
  }
  for (int i = 1; i < 5; ++i) {
    EXPECT_EQ(0, scheduler_.GetLatestEventWithPrecedence(i));
  }
  for (int i = 1; i < 5; ++i) {
    scheduler_.RecordStreamEventTime(i, i * 100);
  }
  for (int i = 1; i < 5; ++i) {
    EXPECT_EQ((i - 1) * 100, scheduler_.GetLatestEventWithPrecedence(i));
  }
}

// Add ready streams at front and back.
TEST_F(Http2PriorityWriteSchedulerTest, MarkReadyFrontAndBack) {
  scheduler_.RegisterStream(1, 0, 10, false);
  scheduler_.RegisterStream(2, 0, 20, false);
  scheduler_.RegisterStream(3, 0, 20, false);
  scheduler_.RegisterStream(4, 0, 20, false);
  scheduler_.RegisterStream(5, 0, 30, false);

  for (int i = 1; i < 6; ++i) {
    scheduler_.MarkStreamReady(i, false);
  }
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(2, false);
  EXPECT_EQ(3u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(3, false);
  EXPECT_EQ(4u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(4, false);
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(2, true);
  EXPECT_EQ(2u, scheduler_.PopNextReadyStream());
  scheduler_.MarkStreamReady(5, false);
  scheduler_.MarkStreamReady(2, true);
  EXPECT_EQ(5u, scheduler_.PopNextReadyStream());
}

class PopNextReadyStreamTest : public Http2PriorityWriteSchedulerTest {
 protected:
  void SetUp() override {
    /* Create the tree.

             0
            /|\
           1 2 3
          /| |\
         4 5 6 7
        /
       8

    */
    scheduler_.RegisterStream(1, 0, 100, false);
    scheduler_.RegisterStream(2, 0, 100, false);
    scheduler_.RegisterStream(3, 0, 100, false);
    scheduler_.RegisterStream(4, 1, 100, false);
    scheduler_.RegisterStream(5, 1, 100, false);
    scheduler_.RegisterStream(6, 2, 100, false);
    scheduler_.RegisterStream(7, 2, 100, false);
    scheduler_.RegisterStream(8, 4, 100, false);

    // Set all nodes ready to write.
    for (SpdyStreamId id = 1; id <= 8; ++id) {
      scheduler_.MarkStreamReady(id, false);
    }
  }

  AssertionResult PopNextReturnsCycle(
      std::initializer_list<SpdyStreamId> stream_ids) {
    int count = 0;
    const int kNumCyclesToCheck = 2;
    for (int i = 0; i < kNumCyclesToCheck; i++) {
      for (SpdyStreamId expected_id : stream_ids) {
        SpdyStreamId next_id = scheduler_.PopNextReadyStream();
        scheduler_.MarkStreamReady(next_id, false);
        if (next_id != expected_id) {
          return AssertionFailure() << "Pick " << count << ": expected stream "
                                    << expected_id << " instead of " << next_id;
        }
        if (!peer_.ValidateInvariants()) {
          return AssertionFailure() << "ValidateInvariants failed";
        }
        ++count;
      }
    }
    return AssertionSuccess();
  }
};

// When all streams are schedulable, only top-level streams should be returned.
TEST_F(PopNextReadyStreamTest, NoneBlocked) {
  EXPECT_TRUE(PopNextReturnsCycle({1, 2, 3}));
}

// When a parent stream is blocked, its children should be scheduled, if
// priorities allow.
TEST_F(PopNextReadyStreamTest, SingleStreamBlocked) {
  scheduler_.MarkStreamNotReady(1);

  // Round-robin only across 2 and 3, since children of 1 have lower priority.
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));

  // Make children of 1 have equal priority as 2 and 3, after which they should
  // be returned as well.
  scheduler_.UpdateStreamWeight(1, 200);
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 2, 3}));
}

// Block multiple levels of streams.
TEST_F(PopNextReadyStreamTest, MultiLevelBlocked) {
  for (SpdyStreamId stream_id : {1, 4, 5}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  // Round-robin only across 2 and 3, since children of 1 have lower priority.
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));

  // Make 8 have equal priority as 2 and 3.
  scheduler_.UpdateStreamWeight(1, 200);
  EXPECT_TRUE(PopNextReturnsCycle({8, 2, 3}));
}

// A removed stream shouldn't be scheduled.
TEST_F(PopNextReadyStreamTest, RemoveStream) {
  scheduler_.UnregisterStream(1);

  // Round-robin only across 2 and 3, since previous children of 1 have lower
  // priority (the weight of 4 and 5 is scaled down when they are elevated to
  // siblings of 2 and 3).
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));

  // Make previous children of 1 have equal priority as 2 and 3.
  scheduler_.UpdateStreamWeight(4, 100);
  scheduler_.UpdateStreamWeight(5, 100);
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 2, 3}));
}

// Block an entire subtree.
TEST_F(PopNextReadyStreamTest, SubtreeBlocked) {
  for (SpdyStreamId stream_id : {1, 4, 5, 8}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  EXPECT_TRUE(PopNextReturnsCycle({2, 3}));
}

// If all parent streams are blocked, children should be returned.
TEST_F(PopNextReadyStreamTest, ParentsBlocked) {
  for (SpdyStreamId stream_id : {1, 2, 3}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 6, 7}));
}

// Unblocking streams should make them schedulable.
TEST_F(PopNextReadyStreamTest, BlockAndUnblock) {
  EXPECT_TRUE(PopNextReturnsCycle({1, 2, 3}));
  scheduler_.MarkStreamNotReady(2);
  EXPECT_TRUE(PopNextReturnsCycle({1, 3}));
  scheduler_.MarkStreamReady(2, false);
  // Cycle order permuted since 2 effectively appended at tail.
  EXPECT_TRUE(PopNextReturnsCycle({1, 3, 2}));
}

// Block nodes in multiple subtrees.
TEST_F(PopNextReadyStreamTest, ScatteredBlocked) {
  for (SpdyStreamId stream_id : {1, 2, 6, 7}) {
    scheduler_.MarkStreamNotReady(stream_id);
  }
  // Only 3 returned, since of remaining streams it has highest priority.
  EXPECT_TRUE(PopNextReturnsCycle({3}));

  // Make children of 1 have priority equal to 3.
  scheduler_.UpdateStreamWeight(1, 200);
  EXPECT_TRUE(PopNextReturnsCycle({4, 5, 3}));

  // When 4 is blocked, its child 8 should take its place, since it has same
  // priority.
  scheduler_.MarkStreamNotReady(4);
  EXPECT_TRUE(PopNextReturnsCycle({8, 5, 3}));
}

}  // namespace test
}  // namespace net
