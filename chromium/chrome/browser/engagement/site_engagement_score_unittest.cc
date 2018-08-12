// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/engagement/site_engagement_score.h"

#include <utility>

#include "base/macros.h"
#include "base/test/simple_test_clock.h"
#include "base/values.h"
#include "chrome/browser/engagement/site_engagement_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const int kLessAccumulationsThanNeededToMaxDailyEngagement = 2;
const int kMoreAccumulationsThanNeededToMaxDailyEngagement = 40;
const int kMoreAccumulationsThanNeededToMaxTotalEngagement = 200;
const int kLessDaysThanNeededToMaxTotalEngagement = 4;
const int kMoreDaysThanNeededToMaxTotalEngagement = 40;
const int kLessPeriodsThanNeededToDecayMaxScore = 2;
const int kMorePeriodsThanNeededToDecayMaxScore = 40;

base::Time GetReferenceTime() {
  base::Time::Exploded exploded_reference_time;
  exploded_reference_time.year = 2015;
  exploded_reference_time.month = 1;
  exploded_reference_time.day_of_month = 30;
  exploded_reference_time.day_of_week = 5;
  exploded_reference_time.hour = 11;
  exploded_reference_time.minute = 0;
  exploded_reference_time.second = 0;
  exploded_reference_time.millisecond = 0;

  return base::Time::FromLocalExploded(exploded_reference_time);
}

}  // namespace

class SiteEngagementScoreTest : public testing::Test {
 public:
  SiteEngagementScoreTest() : score_(&test_clock_) {}

  void SetUp() override {
    testing::Test::SetUp();
    // Disable the first engagement bonus for tests.
    SiteEngagementScore::SetParamValuesForTesting();
  }

 protected:
  void VerifyScore(const SiteEngagementScore& score,
                   double expected_raw_score,
                   double expected_points_added_today,
                   base::Time expected_last_engagement_time) {
    EXPECT_EQ(expected_raw_score, score.raw_score_);
    EXPECT_EQ(expected_points_added_today, score.points_added_today_);
    EXPECT_EQ(expected_last_engagement_time, score.last_engagement_time_);
  }

  void UpdateScore(SiteEngagementScore* score,
                   double raw_score,
                   double points_added_today,
                   base::Time last_engagement_time) {
    score->raw_score_ = raw_score;
    score->points_added_today_ = points_added_today;
    score->last_engagement_time_ = last_engagement_time;
  }

  void TestScoreInitializesAndUpdates(
      base::DictionaryValue* score_dict,
      double expected_raw_score,
      double expected_points_added_today,
      base::Time expected_last_engagement_time) {
    SiteEngagementScore initial_score(&test_clock_, *score_dict);
    VerifyScore(initial_score, expected_raw_score, expected_points_added_today,
                expected_last_engagement_time);

    // Updating the score dict should return false, as the score shouldn't
    // have changed at this point.
    EXPECT_FALSE(initial_score.UpdateScoreDict(score_dict));

    // Update the score to new values and verify it updates the score dict
    // correctly.
    base::Time different_day =
        GetReferenceTime() + base::TimeDelta::FromDays(1);
    UpdateScore(&initial_score, 5, 10, different_day);
    EXPECT_TRUE(initial_score.UpdateScoreDict(score_dict));
    SiteEngagementScore updated_score(&test_clock_, *score_dict);
    VerifyScore(updated_score, 5, 10, different_day);
  }

  void SetFirstDailyEngagementPointsForTesting(double points) {
    SiteEngagementScore::param_values
        [SiteEngagementScore::FIRST_DAILY_ENGAGEMENT] = points;
  }

  base::SimpleTestClock test_clock_;
  SiteEngagementScore score_;
};

// Accumulate score many times on the same day. Ensure each time the score goes
// up, but not more than the maximum per day.
TEST_F(SiteEngagementScoreTest, AccumulateOnSameDay) {
  base::Time reference_time = GetReferenceTime();

  test_clock_.SetNow(reference_time);
  for (int i = 0; i < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++i) {
    score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
    EXPECT_EQ(std::min(SiteEngagementScore::GetMaxPointsPerDay(),
                       (i + 1) * SiteEngagementScore::GetNavigationPoints()),
              score_.GetScore());
  }

  EXPECT_EQ(SiteEngagementScore::GetMaxPointsPerDay(), score_.GetScore());
}

// Accumulate on the first day to max that day's engagement, then accumulate on
// a different day.
TEST_F(SiteEngagementScoreTest, AccumulateOnTwoDays) {
  base::Time reference_time = GetReferenceTime();
  base::Time later_date = reference_time + base::TimeDelta::FromDays(2);

  test_clock_.SetNow(reference_time);
  for (int i = 0; i < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++i)
    score_.AddPoints(SiteEngagementScore::GetNavigationPoints());

  EXPECT_EQ(SiteEngagementScore::GetMaxPointsPerDay(), score_.GetScore());

  test_clock_.SetNow(later_date);
  for (int i = 0; i < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++i) {
    score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
    double day_score =
        std::min(SiteEngagementScore::GetMaxPointsPerDay(),
                 (i + 1) * SiteEngagementScore::GetNavigationPoints());
    EXPECT_EQ(day_score + SiteEngagementScore::GetMaxPointsPerDay(),
              score_.GetScore());
  }

  EXPECT_EQ(2 * SiteEngagementScore::GetMaxPointsPerDay(), score_.GetScore());
}

// Accumulate score on many consecutive days and ensure the score doesn't exceed
// the maximum allowed.
TEST_F(SiteEngagementScoreTest, AccumulateALotOnManyDays) {
  base::Time current_day = GetReferenceTime();

  for (int i = 0; i < kMoreDaysThanNeededToMaxTotalEngagement; ++i) {
    current_day += base::TimeDelta::FromDays(1);
    test_clock_.SetNow(current_day);
    for (int j = 0; j < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++j)
      score_.AddPoints(SiteEngagementScore::GetNavigationPoints());

    EXPECT_EQ(std::min(SiteEngagementScore::kMaxPoints,
                       (i + 1) * SiteEngagementScore::GetMaxPointsPerDay()),
              score_.GetScore());
  }

  EXPECT_EQ(SiteEngagementScore::kMaxPoints, score_.GetScore());
}

// Accumulate a little on many consecutive days and ensure the score doesn't
// exceed the maximum allowed.
TEST_F(SiteEngagementScoreTest, AccumulateALittleOnManyDays) {
  base::Time current_day = GetReferenceTime();

  for (int i = 0; i < kMoreAccumulationsThanNeededToMaxTotalEngagement; ++i) {
    current_day += base::TimeDelta::FromDays(1);
    test_clock_.SetNow(current_day);

    for (int j = 0; j < kLessAccumulationsThanNeededToMaxDailyEngagement; ++j)
      score_.AddPoints(SiteEngagementScore::GetNavigationPoints());

    EXPECT_EQ(
        std::min(SiteEngagementScore::kMaxPoints,
                 (i + 1) * kLessAccumulationsThanNeededToMaxDailyEngagement *
                     SiteEngagementScore::GetNavigationPoints()),
        score_.GetScore());
  }

  EXPECT_EQ(SiteEngagementScore::kMaxPoints, score_.GetScore());
}

// Accumulate a bit, then check the score decays properly for a range of times.
TEST_F(SiteEngagementScoreTest, ScoresDecayOverTime) {
  base::Time current_day = GetReferenceTime();

  // First max the score.
  for (int i = 0; i < kMoreDaysThanNeededToMaxTotalEngagement; ++i) {
    current_day += base::TimeDelta::FromDays(1);
    test_clock_.SetNow(current_day);

    for (int j = 0; j < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++j)
      score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
  }

  EXPECT_EQ(SiteEngagementScore::kMaxPoints, score_.GetScore());

  // The score should not have decayed before the first decay period has
  // elapsed.
  test_clock_.SetNow(current_day +
                     base::TimeDelta::FromDays(
                         SiteEngagementScore::GetDecayPeriodInDays() - 1));
  EXPECT_EQ(SiteEngagementScore::kMaxPoints, score_.GetScore());

  // The score should have decayed by one chunk after one decay period has
  // elapsed.
  test_clock_.SetNow(
      current_day +
      base::TimeDelta::FromDays(SiteEngagementScore::GetDecayPeriodInDays()));
  EXPECT_EQ(
      SiteEngagementScore::kMaxPoints - SiteEngagementScore::GetDecayPoints(),
      score_.GetScore());

  // The score should have decayed by the right number of chunks after a few
  // decay periods have elapsed.
  test_clock_.SetNow(
      current_day +
      base::TimeDelta::FromDays(kLessPeriodsThanNeededToDecayMaxScore *
                                SiteEngagementScore::GetDecayPeriodInDays()));
  EXPECT_EQ(SiteEngagementScore::kMaxPoints -
                kLessPeriodsThanNeededToDecayMaxScore *
                    SiteEngagementScore::GetDecayPoints(),
            score_.GetScore());

  // The score should not decay below zero.
  test_clock_.SetNow(
      current_day +
      base::TimeDelta::FromDays(kMorePeriodsThanNeededToDecayMaxScore *
                                SiteEngagementScore::GetDecayPeriodInDays()));
  EXPECT_EQ(0, score_.GetScore());
}

// Test that any expected decays are applied before adding points.
TEST_F(SiteEngagementScoreTest, DecaysAppliedBeforeAdd) {
  base::Time current_day = GetReferenceTime();

  // Get the score up to something that can handle a bit of decay before
  for (int i = 0; i < kLessDaysThanNeededToMaxTotalEngagement; ++i) {
    current_day += base::TimeDelta::FromDays(1);
    test_clock_.SetNow(current_day);

    for (int j = 0; j < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++j)
      score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
  }

  double initial_score = kLessDaysThanNeededToMaxTotalEngagement *
                         SiteEngagementScore::GetMaxPointsPerDay();
  EXPECT_EQ(initial_score, score_.GetScore());

  // Go forward a few decay periods.
  test_clock_.SetNow(
      current_day +
      base::TimeDelta::FromDays(kLessPeriodsThanNeededToDecayMaxScore *
                                SiteEngagementScore::GetDecayPeriodInDays()));

  double decayed_score = initial_score -
                         kLessPeriodsThanNeededToDecayMaxScore *
                             SiteEngagementScore::GetDecayPoints();
  EXPECT_EQ(decayed_score, score_.GetScore());

  // Now add some points.
  score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
  EXPECT_EQ(decayed_score + SiteEngagementScore::GetNavigationPoints(),
            score_.GetScore());
}

// Test that going back in time is handled properly.
TEST_F(SiteEngagementScoreTest, GoBackInTime) {
  base::Time current_day = GetReferenceTime();

  test_clock_.SetNow(current_day);
  for (int i = 0; i < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++i)
    score_.AddPoints(SiteEngagementScore::GetNavigationPoints());

  EXPECT_EQ(SiteEngagementScore::GetMaxPointsPerDay(), score_.GetScore());

  // Adding to the score on an earlier date should be treated like another day,
  // and should not cause any decay.
  test_clock_.SetNow(current_day - base::TimeDelta::FromDays(
                                       kMorePeriodsThanNeededToDecayMaxScore *
                                       SiteEngagementScore::GetDecayPoints()));
  for (int i = 0; i < kMoreAccumulationsThanNeededToMaxDailyEngagement; ++i) {
    score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
    double day_score =
        std::min(SiteEngagementScore::GetMaxPointsPerDay(),
                 (i + 1) * SiteEngagementScore::GetNavigationPoints());
    EXPECT_EQ(day_score + SiteEngagementScore::GetMaxPointsPerDay(),
              score_.GetScore());
  }

  EXPECT_EQ(2 * SiteEngagementScore::GetMaxPointsPerDay(), score_.GetScore());
}

// Test that scores are read / written correctly from / to empty score
// dictionaries.
TEST_F(SiteEngagementScoreTest, EmptyDictionary) {
  base::DictionaryValue dict;
  TestScoreInitializesAndUpdates(&dict, 0, 0, base::Time());
}

// Test that scores are read / written correctly from / to partially empty
// score dictionaries.
TEST_F(SiteEngagementScoreTest, PartiallyEmptyDictionary) {
  base::DictionaryValue dict;
  dict.SetDouble(SiteEngagementScore::kPointsAddedTodayKey, 2);

  TestScoreInitializesAndUpdates(&dict, 0, 2, base::Time());
}

// Test that scores are read / written correctly from / to populated score
// dictionaries.
TEST_F(SiteEngagementScoreTest, PopulatedDictionary) {
  base::DictionaryValue dict;
  dict.SetDouble(SiteEngagementScore::kRawScoreKey, 1);
  dict.SetDouble(SiteEngagementScore::kPointsAddedTodayKey, 2);
  dict.SetDouble(SiteEngagementScore::kLastEngagementTimeKey,
                 GetReferenceTime().ToInternalValue());

  TestScoreInitializesAndUpdates(&dict, 1, 2, GetReferenceTime());
}

// Ensure bonus engagement is awarded for the first engagement of a day.
TEST_F(SiteEngagementScoreTest, FirstDailyEngagementBonus) {
  SetFirstDailyEngagementPointsForTesting(0.5);

  SiteEngagementScore score1(&test_clock_);
  SiteEngagementScore score2(&test_clock_);
  base::Time current_day = GetReferenceTime();

  test_clock_.SetNow(current_day);

  // The first engagement event gets the bonus.
  score1.AddPoints(0.5);
  EXPECT_EQ(1.0, score1.GetScore());

  // Subsequent events do not.
  score1.AddPoints(0.5);
  EXPECT_EQ(1.5, score1.GetScore());

  // Bonuses are awarded independently between scores.
  score2.AddPoints(1.0);
  EXPECT_EQ(1.5, score2.GetScore());
  score2.AddPoints(1.0);
  EXPECT_EQ(2.5, score2.GetScore());

  test_clock_.SetNow(current_day + base::TimeDelta::FromDays(1));

  // The first event for the next day gets the bonus.
  score1.AddPoints(0.5);
  EXPECT_EQ(2.5, score1.GetScore());

  // Subsequent events do not.
  score1.AddPoints(0.5);
  EXPECT_EQ(3.0, score1.GetScore());

  score2.AddPoints(1.0);
  EXPECT_EQ(4.0, score2.GetScore());
  score2.AddPoints(1.0);
  EXPECT_EQ(5.0, score2.GetScore());
}

// Test that resetting a score has the correct properties.
TEST_F(SiteEngagementScoreTest, Reset) {
  base::Time current_day = GetReferenceTime();

  test_clock_.SetNow(current_day);
  score_.AddPoints(SiteEngagementScore::GetNavigationPoints());
  EXPECT_EQ(SiteEngagementScore::GetNavigationPoints(), score_.GetScore());

  current_day += base::TimeDelta::FromDays(7);
  test_clock_.SetNow(current_day);

  score_.Reset(20.0, nullptr);
  EXPECT_DOUBLE_EQ(20.0, score_.GetScore());
  EXPECT_DOUBLE_EQ(0, score_.points_added_today_);
  EXPECT_EQ(current_day, score_.last_engagement_time_);
  EXPECT_TRUE(score_.last_shortcut_launch_time_.is_null());

  // Adding points after the reset should work as normal.
  score_.AddPoints(5);
  EXPECT_EQ(25.0, score_.GetScore());

  // The decay should happen one decay period from the current time.
  test_clock_.SetNow(current_day +
                     base::TimeDelta::FromDays(
                         SiteEngagementScore::GetDecayPeriodInDays() + 1));
  EXPECT_EQ(25.0 - SiteEngagementScore::GetDecayPoints(), score_.GetScore());

  // Ensure that manually setting a time works as expected.
  score_.AddPoints(5);
  test_clock_.SetNow(GetReferenceTime());
  base::Time now = test_clock_.Now();
  score_.Reset(10.0, &now);

  EXPECT_DOUBLE_EQ(10.0, score_.GetScore());
  EXPECT_DOUBLE_EQ(0, score_.points_added_today_);
  EXPECT_EQ(now, score_.last_engagement_time_);
  EXPECT_TRUE(score_.last_shortcut_launch_time_.is_null());

  score_.set_last_shortcut_launch_time(test_clock_.Now());
  test_clock_.SetNow(GetReferenceTime() + base::TimeDelta::FromDays(3));
  now = test_clock_.Now();
  score_.Reset(15.0, &now);

  // 5 bonus from the last shortcut launch.
  EXPECT_DOUBLE_EQ(20.0, score_.GetScore());
  EXPECT_DOUBLE_EQ(0, score_.points_added_today_);
  EXPECT_EQ(now, score_.last_engagement_time_);
  EXPECT_EQ(now, score_.last_shortcut_launch_time_);
}
