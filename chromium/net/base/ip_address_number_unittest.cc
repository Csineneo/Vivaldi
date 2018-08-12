// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/ip_address_number.h"

#include "base/format_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

// Helper to strignize an IP number (used to define expectations).
std::string DumpIPNumber(const IPAddressNumber& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i != 0)
      out.append(",");
    out.append(base::IntToString(static_cast<int>(v[i])));
  }
  return out;
}

TEST(IpAddressNumberTest, IPAddressToString) {
  uint8_t addr1[4] = {0, 0, 0, 0};
  EXPECT_EQ("0.0.0.0", IPAddressToString(addr1, sizeof(addr1)));

  uint8_t addr2[4] = {192, 168, 0, 1};
  EXPECT_EQ("192.168.0.1", IPAddressToString(addr2, sizeof(addr2)));

  uint8_t addr3[16] = {0xFE, 0xDC, 0xBA, 0x98};
  EXPECT_EQ("fedc:ba98::", IPAddressToString(addr3, sizeof(addr3)));

  // IPAddressToString() shouldn't crash on invalid addresses.
  uint8_t addr4[2];
  EXPECT_EQ("", IPAddressToString(addr4, sizeof(addr4)));
}

TEST(IpAddressNumberTest, IPAddressToStringWithPort) {
  uint8_t addr1[4] = {0, 0, 0, 0};
  EXPECT_EQ("0.0.0.0:3", IPAddressToStringWithPort(addr1, sizeof(addr1), 3));

  uint8_t addr2[4] = {192, 168, 0, 1};
  EXPECT_EQ("192.168.0.1:99",
            IPAddressToStringWithPort(addr2, sizeof(addr2), 99));

  uint8_t addr3[16] = {0xFE, 0xDC, 0xBA, 0x98};
  EXPECT_EQ("[fedc:ba98::]:8080",
            IPAddressToStringWithPort(addr3, sizeof(addr3), 8080));

  // IPAddressToStringWithPort() shouldn't crash on invalid addresses.
  uint8_t addr4[2];
  EXPECT_EQ("", IPAddressToStringWithPort(addr4, sizeof(addr4), 8080));
}

// Test that invalid IP literals fail to parse.
TEST(IpAddressNumberTest, ParseIPLiteralToNumber_FailParse) {
  IPAddressNumber number;

  EXPECT_FALSE(ParseIPLiteralToNumber("bad value", &number));
  EXPECT_FALSE(ParseIPLiteralToNumber("bad:value", &number));
  EXPECT_FALSE(ParseIPLiteralToNumber(std::string(), &number));
  EXPECT_FALSE(ParseIPLiteralToNumber("192.168.0.1:30", &number));
  EXPECT_FALSE(ParseIPLiteralToNumber("  192.168.0.1  ", &number));
  EXPECT_FALSE(ParseIPLiteralToNumber("[::1]", &number));
}

// Test parsing an IPv4 literal.
TEST(IpAddressNumberTest, ParseIPLiteralToNumber_IPv4) {
  IPAddressNumber number;
  EXPECT_TRUE(ParseIPLiteralToNumber("192.168.0.1", &number));
  EXPECT_EQ("192,168,0,1", DumpIPNumber(number));
  EXPECT_EQ("192.168.0.1", IPAddressToString(number));
}

// Test parsing an IPv6 literal.
TEST(IpAddressNumberTest, ParseIPLiteralToNumber_IPv6) {
  IPAddressNumber number;
  EXPECT_TRUE(ParseIPLiteralToNumber("1:abcd::3:4:ff", &number));
  EXPECT_EQ("0,1,171,205,0,0,0,0,0,0,0,3,0,4,0,255", DumpIPNumber(number));
  EXPECT_EQ("1:abcd::3:4:ff", IPAddressToString(number));
}

// Test mapping an IPv4 address to an IPv6 address.
TEST(IpAddressNumberTest, ConvertIPv4NumberToIPv6Number) {
  IPAddressNumber ipv4_number;
  EXPECT_TRUE(ParseIPLiteralToNumber("192.168.0.1", &ipv4_number));

  IPAddressNumber ipv6_number =
      ConvertIPv4NumberToIPv6Number(ipv4_number);

  // ::ffff:192.168.0.1
  EXPECT_EQ("0,0,0,0,0,0,0,0,0,0,255,255,192,168,0,1",
            DumpIPNumber(ipv6_number));
  EXPECT_EQ("::ffff:c0a8:1", IPAddressToString(ipv6_number));
}

TEST(IpAddressNumberTest, IsIPv4Mapped) {
  IPAddressNumber ipv4_number;
  EXPECT_TRUE(ParseIPLiteralToNumber("192.168.0.1", &ipv4_number));
  EXPECT_FALSE(IsIPv4Mapped(ipv4_number));

  IPAddressNumber ipv6_number;
  EXPECT_TRUE(ParseIPLiteralToNumber("::1", &ipv6_number));
  EXPECT_FALSE(IsIPv4Mapped(ipv6_number));

  IPAddressNumber ipv4mapped_number;
  EXPECT_TRUE(ParseIPLiteralToNumber("::ffff:0101:1", &ipv4mapped_number));
  EXPECT_TRUE(IsIPv4Mapped(ipv4mapped_number));
}

TEST(IpAddressNumberTest, ConvertIPv4MappedToIPv4) {
  IPAddressNumber ipv4mapped_number;
  EXPECT_TRUE(ParseIPLiteralToNumber("::ffff:0101:1", &ipv4mapped_number));
  IPAddressNumber expected;
  EXPECT_TRUE(ParseIPLiteralToNumber("1.1.0.1", &expected));
  IPAddressNumber result = ConvertIPv4MappedToIPv4(ipv4mapped_number);
  EXPECT_EQ(expected, result);
}

TEST(IpAddressNumberTest, IPNumberMatchesPrefix) {
  struct {
    const char* const cidr_literal;
    size_t prefix_length_in_bits;
    const char* const ip_literal;
    bool expected_to_match;
  } tests[] = {
      // IPv4 prefix with IPv4 inputs.
      {"10.10.1.32", 27, "10.10.1.44", true},
      {"10.10.1.32", 27, "10.10.1.90", false},
      {"10.10.1.32", 27, "10.10.1.90", false},

      // IPv6 prefix with IPv6 inputs.
      {"2001:db8::", 32, "2001:DB8:3:4::5", true},
      {"2001:db8::", 32, "2001:c8::", false},

      // IPv6 prefix with IPv4 inputs.
      {"2001:db8::", 33, "192.168.0.1", false},
      {"::ffff:192.168.0.1", 112, "192.168.33.77", true},

      // IPv4 prefix with IPv6 inputs.
      {"10.11.33.44", 16, "::ffff:0a0b:89", true},
      {"10.11.33.44", 16, "::ffff:10.12.33.44", false},
  };
  for (size_t i = 0; i < arraysize(tests); ++i) {
    SCOPED_TRACE(base::StringPrintf("Test[%" PRIuS "]: %s, %s", i,
                                    tests[i].cidr_literal,
                                    tests[i].ip_literal));

    IPAddressNumber ip_number;
    EXPECT_TRUE(ParseIPLiteralToNumber(tests[i].ip_literal, &ip_number));

    IPAddressNumber ip_prefix;

    EXPECT_TRUE(ParseIPLiteralToNumber(tests[i].cidr_literal, &ip_prefix));

    EXPECT_EQ(tests[i].expected_to_match,
              IPNumberMatchesPrefix(ip_number, ip_prefix,
                                    tests[i].prefix_length_in_bits));
  }
}

}  // anonymous namespace

}  // namespace net
