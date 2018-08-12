// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ENGAGEMENT_SITE_ENGAGEMENT_SCORE_H_
#define CHROME_BROWSER_ENGAGEMENT_SITE_ENGAGEMENT_SCORE_H_

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "base/values.h"

namespace base {
class Clock;
}

class SiteEngagementScore {
 public:
  // The parameters which can be varied via field trial. All "points" values
  // should be appended to the end of the enum prior to MAX_VARIATION.
  enum Variation {
    // The maximum number of points that can be accrued in one day.
    MAX_POINTS_PER_DAY = 0,

    // The period over which site engagement decays.
    DECAY_PERIOD_IN_DAYS,

    // The number of points to decay per period.
    DECAY_POINTS,

    // The number of points given for navigations.
    NAVIGATION_POINTS,

    // The number of points given for user input.
    USER_INPUT_POINTS,

    // The number of points given for media playing. Initially calibrated such
    // that at least 30 minutes of foreground media would be required to allow a
    // site to reach the daily engagement maximum.
    VISIBLE_MEDIA_POINTS,
    HIDDEN_MEDIA_POINTS,

    // The number of points added to engagement when a site is launched from
    // homescreen or added as a bookmark app. This bonus will apply for ten days
    // following a launch; each new launch resets the ten days.
    WEB_APP_INSTALLED_POINTS,

    // The number of points given for the first engagement event of the day for
    // each site.
    FIRST_DAILY_ENGAGEMENT,

    // The number of points that the engagement service must accumulate to be
    // considered 'useful'.
    BOOTSTRAP_POINTS,

    // The boundaries between low/medium and medium/high engagement as returned
    // by GetEngagementLevel().
    MEDIUM_ENGAGEMENT_BOUNDARY,
    HIGH_ENGAGEMENT_BOUNDARY,

    MAX_VARIATION
  };

  // The maximum number of points that are allowed.
  static const double kMaxPoints;

  static double GetMaxPointsPerDay();
  static double GetDecayPeriodInDays();
  static double GetDecayPoints();
  static double GetNavigationPoints();
  static double GetUserInputPoints();
  static double GetVisibleMediaPoints();
  static double GetHiddenMediaPoints();
  static double GetWebAppInstalledPoints();
  static double GetFirstDailyEngagementPoints();
  static double GetBootstrapPoints();
  static double GetMediumEngagementBoundary();
  static double GetHighEngagementBoundary();

  // Update the default engagement settings via variations.
  static void UpdateFromVariations(const char* param_name);

  // The SiteEngagementScore does not take ownership of |clock|. It is the
  // responsibility of the caller to make sure |clock| outlives this
  // SiteEngagementScore.
  SiteEngagementScore(base::Clock* clock,
                      const base::DictionaryValue& score_dict);
  ~SiteEngagementScore();

  // Adds |points| to this score, respecting daily limits and the maximum
  // possible score. Decays the score if it has not been updated recently
  // enough.
  void AddPoints(double points);
  double GetScore() const;

  // Returns true if the maximum number of points today has been added.
  bool MaxPointsPerDayAdded() const;

  // Resets the score to |points| and resets the daily point limit. If
  // |updated_time| is non-null, sets the last engagement time and last
  // shortcut launch time (if it is non-null) to |updated_time|. Otherwise, last
  // engagement time is set to the current time and last shortcut launch time is
  // left unchanged.
  // TODO(calamity): Ideally, all SiteEngagementScore methods should take a
  // base::Time argument like this one does rather than each SiteEngagementScore
  // hold a pointer to a base::Clock. Then SiteEngagementScore doesn't need to
  // worry about clock vending. See crbug.com/604305.
  void Reset(double points, const base::Time* updated_time);

  // Updates the content settings dictionary |score_dict| with the current score
  // fields. Returns true if |score_dict| changed, otherwise return false.
  bool UpdateScoreDict(base::DictionaryValue* score_dict);

  // Get/set the last time this origin was launched from an installed shortcut.
  base::Time last_shortcut_launch_time() const {
    return last_shortcut_launch_time_;
  }
  void set_last_shortcut_launch_time(const base::Time& time) {
    last_shortcut_launch_time_ = time;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(SiteEngagementScoreTest, PartiallyEmptyDictionary);
  FRIEND_TEST_ALL_PREFIXES(SiteEngagementScoreTest, PopulatedDictionary);
  FRIEND_TEST_ALL_PREFIXES(SiteEngagementScoreTest, Reset);
  FRIEND_TEST_ALL_PREFIXES(SiteEngagementScoreTest, FirstDailyEngagementBonus);
  friend class ImportantSitesUtilTest;
  friend class SiteEngagementHelperTest;
  friend class SiteEngagementScoreTest;
  friend class SiteEngagementServiceTest;

  // Array holding the values corresponding to each item in Variation array.
  static double param_values[];

  // Keys used in the content settings dictionary.
  static const char* kRawScoreKey;
  static const char* kPointsAddedTodayKey;
  static const char* kLastEngagementTimeKey;
  static const char* kLastShortcutLaunchTimeKey;

  // This version of the constructor is used in unit tests.
  explicit SiteEngagementScore(base::Clock* clock);

  // Determine the score, accounting for any decay.
  double DecayedScore() const;

  // Determine any score bonus from having installed shortcuts.
  double BonusScore() const;

  // Sets fixed parameter values for testing site engagement. Ensure that any
  // newly added parameters receive a fixed value here.
  static void SetParamValuesForTesting();

  // The clock used to vend times. Enables time travelling in tests. Owned by
  // the SiteEngagementService.
  base::Clock* clock_;

  // |raw_score_| is the score before any decay is applied.
  double raw_score_;

  // The points added 'today' are tracked to avoid adding more than
  // kMaxPointsPerDay on any one day. 'Today' is defined in local time.
  double points_added_today_;

  // The last time the score was updated for engagement. Used in conjunction
  // with |points_added_today_| to avoid adding more than kMaxPointsPerDay on
  // any one day.
  base::Time last_engagement_time_;

  // The last time the site with this score was launched from an installed
  // shortcut.
  base::Time last_shortcut_launch_time_;

  DISALLOW_COPY_AND_ASSIGN(SiteEngagementScore);
};

#endif  // CHROME_BROWSER_ENGAGEMENT_SITE_ENGAGEMENT_SCORE_H_
