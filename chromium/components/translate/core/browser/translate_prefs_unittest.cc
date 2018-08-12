// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/translate/core/browser/translate_prefs.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "components/pref_registry/testing_pref_service_syncable.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/translate/core/browser/translate_download_manager.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kTestLanguage[] = "en";

}  // namespace

namespace translate {

class TranslatePrefTest : public testing::Test {
 protected:
  TranslatePrefTest() : prefs_(new user_prefs::TestingPrefServiceSyncable()) {
#if defined(OS_CHROMEOS)
    const char* preferred_languages_prefs =
        "settings.language.preferred_languages";
#else
    const char* preferred_languages_prefs = NULL;
#endif
    translate_prefs_.reset(new translate::TranslatePrefs(
        prefs_.get(), "intl.accept_languages", preferred_languages_prefs));
    TranslatePrefs::RegisterProfilePrefs(prefs_->registry());
    now_ = base::Time::Now();
    two_days_ago_ = now_ - base::TimeDelta::FromDays(2);
  }

  void SetLastDeniedTime(const std::string& language, base::Time time) {
    DenialTimeUpdate update(prefs_.get(), language, 2);
    update.AddDenialTime(time);
  }

  base::Time GetLastDeniedTime(const std::string& language) {
    DenialTimeUpdate update(prefs_.get(), language, 2);
    return update.GetOldestDenialTime();
  }

  void SetUp() override {
    base::FeatureList::ClearInstanceForTesting();
    base::FeatureList::SetInstance(base::WrapUnique(new base::FeatureList()));
  }

  void TurnOnTranslate2016Q2UIFlag() {
    base::FeatureList::ClearInstanceForTesting();
    std::unique_ptr<base::FeatureList> feature_list(new base::FeatureList);
    feature_list->InitializeFromCommandLine(translate::kTranslateUI2016Q2.name,
                                            std::string());
    base::FeatureList::SetInstance(std::move(feature_list));
  }

  std::unique_ptr<user_prefs::TestingPrefServiceSyncable> prefs_;
  std::unique_ptr<translate::TranslatePrefs> translate_prefs_;

  // Shared time constants.
  base::Time now_;
  base::Time two_days_ago_;
};

TEST_F(TranslatePrefTest, IsTooOftenDeniedIn2016Q2UI) {
  TurnOnTranslate2016Q2UIFlag();

  translate_prefs_->ResetDenialState();
  EXPECT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));

  for (int i = 0; i < 3; i++) {
    translate_prefs_->IncrementTranslationDeniedCount(kTestLanguage);
    EXPECT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));
  }

  translate_prefs_->IncrementTranslationDeniedCount(kTestLanguage);
  EXPECT_TRUE(translate_prefs_->IsTooOftenDenied(kTestLanguage));
}

TEST_F(TranslatePrefTest, IsTooOftenIgnoredIn2016Q2UI) {
  TurnOnTranslate2016Q2UIFlag();

  translate_prefs_->ResetDenialState();
  EXPECT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));

  for (int i = 0; i < 10; i++) {
    translate_prefs_->IncrementTranslationIgnoredCount(kTestLanguage);
    EXPECT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));
  }

  translate_prefs_->IncrementTranslationIgnoredCount(kTestLanguage);
  EXPECT_TRUE(translate_prefs_->IsTooOftenDenied(kTestLanguage));
}

TEST_F(TranslatePrefTest, UpdateLastDeniedTime) {
  // Test that denials with more than 24 hours difference between them do not
  // block the language.
  translate_prefs_->ResetDenialState();
  SetLastDeniedTime(kTestLanguage, two_days_ago_);
  ASSERT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));
  translate_prefs_->UpdateLastDeniedTime(kTestLanguage);
  base::Time last_denied = GetLastDeniedTime(kTestLanguage);
  EXPECT_FALSE(last_denied.is_max());
  EXPECT_GE(last_denied, now_);
  EXPECT_LT(last_denied - now_, base::TimeDelta::FromSeconds(10));
  EXPECT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));

  // Ensure the first use simply writes the update time.
  translate_prefs_->ResetDenialState();
  translate_prefs_->UpdateLastDeniedTime(kTestLanguage);
  last_denied = GetLastDeniedTime(kTestLanguage);
  EXPECT_FALSE(last_denied.is_max());
  EXPECT_GE(last_denied, now_);
  EXPECT_LT(last_denied - now_, base::TimeDelta::FromSeconds(10));
  EXPECT_FALSE(translate_prefs_->IsTooOftenDenied(kTestLanguage));

  // If it's denied again within the 24 hour period, language should be
  // permanently denied.
  translate_prefs_->UpdateLastDeniedTime(kTestLanguage);
  last_denied = GetLastDeniedTime(kTestLanguage);
  EXPECT_FALSE(last_denied.is_max());
  EXPECT_GE(last_denied, now_);
  EXPECT_LT(last_denied - now_, base::TimeDelta::FromSeconds(10));
  EXPECT_TRUE(translate_prefs_->IsTooOftenDenied(kTestLanguage));

  // If the language is already permanently denied, don't bother updating the
  // last_denied time.
  ASSERT_TRUE(translate_prefs_->IsTooOftenDenied(kTestLanguage));
  SetLastDeniedTime(kTestLanguage, two_days_ago_);
  translate_prefs_->UpdateLastDeniedTime(kTestLanguage);
  last_denied = GetLastDeniedTime(kTestLanguage);
  EXPECT_EQ(last_denied, two_days_ago_);
}

// Test that the default value for non-existing entries is base::Time::Null().
TEST_F(TranslatePrefTest, DenialTimeUpdate_DefaultTimeIsNull) {
  DenialTimeUpdate update(prefs_.get(), kTestLanguage, 2);
  EXPECT_TRUE(update.GetOldestDenialTime().is_null());
}

// Test that non-existing entries automatically create a ListValue.
TEST_F(TranslatePrefTest, DenialTimeUpdate_ForceListExistence) {
  DictionaryPrefUpdate dict_update(
      prefs_.get(), TranslatePrefs::kPrefTranslateLastDeniedTimeForLanguage);
  base::DictionaryValue* denial_dict = dict_update.Get();
  EXPECT_TRUE(denial_dict);

  base::ListValue* list_value = nullptr;
  bool has_list = denial_dict->GetList(kTestLanguage, &list_value);
  EXPECT_FALSE(has_list);

  // Calling GetDenialTimes will force creation of a properly populated list.
  DenialTimeUpdate update(prefs_.get(), kTestLanguage, 2);
  base::ListValue* time_list = update.GetDenialTimes();
  EXPECT_TRUE(time_list);
  EXPECT_EQ(0U, time_list->GetSize());
}

// Test that an existing update time record (which is a double in a dict)
// is automatically migrated to a list of update times instead.
TEST_F(TranslatePrefTest, DenialTimeUpdate_Migrate) {
  translate_prefs_->ResetDenialState();
  DictionaryPrefUpdate dict_update(
      prefs_.get(), TranslatePrefs::kPrefTranslateLastDeniedTimeForLanguage);
  base::DictionaryValue* denial_dict = dict_update.Get();
  EXPECT_TRUE(denial_dict);
  denial_dict->SetDouble(kTestLanguage, two_days_ago_.ToJsTime());

  base::ListValue* list_value = nullptr;
  bool has_list = denial_dict->GetList(kTestLanguage, &list_value);
  EXPECT_FALSE(has_list);

  // Calling GetDenialTimes will force creation of a properly populated list.
  DenialTimeUpdate update(prefs_.get(), kTestLanguage, 2);
  base::ListValue* time_list = update.GetDenialTimes();
  EXPECT_TRUE(time_list);

  has_list = denial_dict->GetList(kTestLanguage, &list_value);
  EXPECT_TRUE(has_list);
  EXPECT_EQ(time_list, list_value);
  EXPECT_EQ(1U, time_list->GetSize());
  EXPECT_EQ(two_days_ago_, update.GetOldestDenialTime());
}

TEST_F(TranslatePrefTest, DenialTimeUpdate_SlidingWindow) {
  DenialTimeUpdate update(prefs_.get(), kTestLanguage, 4);

  update.AddDenialTime(now_ - base::TimeDelta::FromMinutes(5));
  EXPECT_EQ(update.GetOldestDenialTime(),
            now_ - base::TimeDelta::FromMinutes(5));

  update.AddDenialTime(now_ - base::TimeDelta::FromMinutes(4));
  EXPECT_EQ(update.GetOldestDenialTime(),
            now_ - base::TimeDelta::FromMinutes(5));

  update.AddDenialTime(now_ - base::TimeDelta::FromMinutes(3));
  EXPECT_EQ(update.GetOldestDenialTime(),
            now_ - base::TimeDelta::FromMinutes(5));

  update.AddDenialTime(now_ - base::TimeDelta::FromMinutes(2));
  EXPECT_EQ(update.GetOldestDenialTime(),
            now_ - base::TimeDelta::FromMinutes(4));

  update.AddDenialTime(now_);
  EXPECT_EQ(update.GetOldestDenialTime(),
            now_ - base::TimeDelta::FromMinutes(3));

  update.AddDenialTime(now_);
  EXPECT_EQ(update.GetOldestDenialTime(),
            now_ - base::TimeDelta::FromMinutes(2));
}

}  // namespace translate
