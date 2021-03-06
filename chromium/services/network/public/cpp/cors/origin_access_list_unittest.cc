// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/cors/origin_access_list.h"
#include "services/network/public/mojom/cors.mojom.h"
#include "services/network/public/mojom/cors_origin_pattern.mojom.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace network {

namespace cors {

namespace {

const auto kAllowSubdomains =
    mojom::CorsOriginAccessMatchMode::kAllowSubdomains;

const auto kDisallowSubdomains =
    mojom::CorsOriginAccessMatchMode::kDisallowSubdomains;

// OriginAccessListTest is a out of blink version of blink::SecurityPolicyTest,
// but it contains only tests for the allow/block lists management.
class OriginAccessListTest : public testing::Test {
 public:
  OriginAccessListTest()
      : https_example_origin_(url::Origin::Create(GURL("https://example.com"))),
        https_sub_example_origin_(
            url::Origin::Create(GURL("https://sub.example.com"))),
        http_example_origin_(url::Origin::Create(GURL("http://example.com"))),
        https_google_origin_(url::Origin::Create(GURL("https://google.com"))),
        source_origin_(url::Origin::Create(GURL("https://chromium.org"))) {}

  ~OriginAccessListTest() override = default;

 protected:
  const url::Origin& https_example_origin() const {
    return https_example_origin_;
  }
  const url::Origin& https_sub_example_origin() const {
    return https_sub_example_origin_;
  }
  const url::Origin& http_example_origin() const {
    return http_example_origin_;
  }
  const url::Origin& https_google_origin() const {
    return https_google_origin_;
  }
  bool IsAllowed(const url::Origin& destination_origin) const {
    return list_.CheckAccessState(source_origin_,
                                  destination_origin.GetURL()) ==
           OriginAccessList::AccessState::kAllowed;
  }
  void SetAllowListEntry(const std::string& protocol,
                         const std::string& host,
                         const mojom::CorsOriginAccessMatchMode mode) {
    std::vector<mojom::CorsOriginPatternPtr> patterns;
    patterns.push_back(mojom::CorsOriginPattern::New(
        protocol, host, mode,
        mojom::CorsOriginAccessMatchPriority::kDefaultPriority));
    list_.SetAllowListForOrigin(source_origin_, patterns);
  }
  void AddAllowListEntry(const std::string& protocol,
                         const std::string& host,
                         const mojom::CorsOriginAccessMatchMode mode,
                         const mojom::CorsOriginAccessMatchPriority priority) {
    list_.AddAllowListEntryForOrigin(source_origin_, protocol, host, mode,
                                     priority);
  }
  void SetBlockListEntry(const std::string& protocol,
                         const std::string& host,
                         const mojom::CorsOriginAccessMatchMode mode) {
    std::vector<mojom::CorsOriginPatternPtr> patterns;
    patterns.push_back(mojom::CorsOriginPattern::New(
        protocol, host, mode,
        mojom::CorsOriginAccessMatchPriority::kDefaultPriority));
    list_.SetBlockListForOrigin(source_origin_, patterns);
  }
  void AddBlockListEntry(const std::string& protocol,
                         const std::string& host,
                         const mojom::CorsOriginAccessMatchMode mode,
                         const mojom::CorsOriginAccessMatchPriority priority) {
    list_.AddBlockListEntryForOrigin(source_origin_, protocol, host, mode,
                                     priority);
  }
  void ResetLists() {
    std::vector<mojom::CorsOriginPatternPtr> patterns;
    list_.SetAllowListForOrigin(source_origin_, patterns);
    list_.SetBlockListForOrigin(source_origin_, patterns);
  }

 private:
  url::Origin https_example_origin_;
  url::Origin https_sub_example_origin_;
  url::Origin http_example_origin_;
  url::Origin https_google_origin_;

  url::Origin source_origin_;

  OriginAccessList list_;

  DISALLOW_COPY_AND_ASSIGN(OriginAccessListTest);
};

TEST_F(OriginAccessListTest, IsAccessAllowed) {
  // By default, no access should be allowed.
  EXPECT_FALSE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));
  EXPECT_FALSE(IsAllowed(http_example_origin()));

  // Adding access for https://example.com should work, but should not grant
  // access to subdomains or other schemes.
  SetAllowListEntry("https", "example.com", kDisallowSubdomains);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));
  EXPECT_FALSE(IsAllowed(http_example_origin()));

  // Clearing the map should revoke all special access.
  ResetLists();
  EXPECT_FALSE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));
  EXPECT_FALSE(IsAllowed(http_example_origin()));

  // Adding an entry that matches subdomains should grant access to any
  // subdomains.
  AddAllowListEntry("https", "example.com", kAllowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kDefaultPriority);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_TRUE(IsAllowed(https_sub_example_origin()));
  EXPECT_FALSE(IsAllowed(http_example_origin()));
}

TEST_F(OriginAccessListTest, IsAccessAllowedWildCard) {
  // An empty domain that matches subdomains results in matching every domain.
  SetAllowListEntry("https", "", kAllowSubdomains);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_TRUE(IsAllowed(https_google_origin()));
  EXPECT_FALSE(IsAllowed(http_example_origin()));
}

TEST_F(OriginAccessListTest, IsAccessAllowedWithBlockListEntry) {
  // The block list takes priority over the allow list.
  SetAllowListEntry("https", "example.com", kAllowSubdomains);
  SetBlockListEntry("https", "example.com", kDisallowSubdomains);

  EXPECT_FALSE(IsAllowed(https_example_origin()));
  EXPECT_TRUE(IsAllowed(https_sub_example_origin()));
}

TEST_F(OriginAccessListTest, IsAccessAllowedWildcardWithBlockListEntry) {
  SetAllowListEntry("https", "", kAllowSubdomains);
  AddBlockListEntry("https", "google.com", kDisallowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kDefaultPriority);

  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_google_origin()));
}

TEST_F(OriginAccessListTest, IsPriorityRespected) {
  SetAllowListEntry("https", "example.com", kAllowSubdomains);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_TRUE(IsAllowed(https_sub_example_origin()));

  // Higher priority blocklist overrides lower priority allowlist.
  AddBlockListEntry("https", "example.com", kAllowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kLowPriority);
  EXPECT_FALSE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));

  // Higher priority allowlist overrides lower priority blocklist.
  AddAllowListEntry("https", "example.com", kDisallowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kMediumPriority);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));
}

TEST_F(OriginAccessListTest, IsPriorityRespectedReverse) {
  AddAllowListEntry("https", "example.com", kDisallowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kMediumPriority);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));

  AddBlockListEntry("https", "example.com", kAllowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kLowPriority);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));

  AddAllowListEntry("https", "example.com", kAllowSubdomains,
                    mojom::CorsOriginAccessMatchPriority::kDefaultPriority);
  EXPECT_TRUE(IsAllowed(https_example_origin()));
  EXPECT_FALSE(IsAllowed(https_sub_example_origin()));
}

TEST_F(OriginAccessListTest, CreateCorsOriginAccessPatternsList) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("https://foo.google.com"));
  const url::Origin kOrigin2 =
      url::Origin::Create(GURL("https://bar.google.com"));
  const std::string kProtocol = "https";
  const std::string kDomain1 = "foo.example.com";
  const std::string kDomain2 = "bar.example.com";

  OriginAccessList list;
  list.AddAllowListEntryForOrigin(
      kOrigin1, kProtocol, kDomain1, kAllowSubdomains,
      mojom::CorsOriginAccessMatchPriority::kMediumPriority);
  list.AddBlockListEntryForOrigin(
      kOrigin2, kProtocol, kDomain2, kDisallowSubdomains,
      mojom::CorsOriginAccessMatchPriority::kDefaultPriority);

  std::vector<mojom::CorsOriginAccessPatternsPtr> patterns =
      list.CreateCorsOriginAccessPatternsList();
  bool found_origin1 = false;
  bool found_origin2 = false;
  for (const auto& pattern : patterns) {
    if (pattern->source_origin == kOrigin1.Serialize()) {
      EXPECT_FALSE(found_origin1);
      found_origin1 = true;

      EXPECT_EQ(0u, pattern->block_patterns.size());
      ASSERT_EQ(1u, pattern->allow_patterns.size());
      EXPECT_EQ(kProtocol, pattern->allow_patterns[0]->protocol);
      EXPECT_EQ(kDomain1, pattern->allow_patterns[0]->domain);
      EXPECT_EQ(kAllowSubdomains, pattern->allow_patterns[0]->mode);
      EXPECT_EQ(mojom::CorsOriginAccessMatchPriority::kMediumPriority,
                pattern->allow_patterns[0]->priority);
    } else if (pattern->source_origin == kOrigin2.Serialize()) {
      EXPECT_FALSE(found_origin2);
      found_origin2 = true;

      EXPECT_EQ(0u, pattern->allow_patterns.size());
      ASSERT_EQ(1u, pattern->block_patterns.size());
      EXPECT_EQ(kProtocol, pattern->block_patterns[0]->protocol);
      EXPECT_EQ(kDomain2, pattern->block_patterns[0]->domain);
      EXPECT_EQ(kDisallowSubdomains, pattern->block_patterns[0]->mode);
      EXPECT_EQ(mojom::CorsOriginAccessMatchPriority::kDefaultPriority,
                pattern->block_patterns[0]->priority);
    } else {
      FAIL();
    }
  }
  EXPECT_TRUE(found_origin1);
  EXPECT_TRUE(found_origin2);
}

}  // namespace

}  // namespace cors

}  // namespace network
