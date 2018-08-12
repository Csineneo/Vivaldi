// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "device/bluetooth/bluetooth_discovery_session.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const device::BluetoothUUID uuid1003("1003");
const device::BluetoothUUID uuid1004("1004");
const device::BluetoothUUID uuid1020("1020");

}  // namespace

namespace device {

TEST(BluetoothDiscoveryFilterTest, Equal) {
  BluetoothDiscoveryFilter df1(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  df1.SetRSSI(-65);
  df1.AddUUID(uuid1020);
  df1.AddUUID(uuid1003);

  BluetoothDiscoveryFilter df2(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  df2.SetRSSI(-65);
  df2.AddUUID(uuid1020);
  df2.AddUUID(uuid1004);

  // uuids are not same, so should fail
  ASSERT_FALSE(df1.Equals(df2));

  // make filters equal
  df1.AddUUID(uuid1004);
  df2.AddUUID(uuid1003);
  ASSERT_TRUE(df1.Equals(df2));

  // now transport don't match
  df1.SetTransport(BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  ASSERT_FALSE(df1.Equals(df2));

  // now everything is back matching
  df1.SetTransport(BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  ASSERT_TRUE(df1.Equals(df2));

  // now rssi don't match
  df1.SetRSSI(-30);
  ASSERT_FALSE(df1.Equals(df2));

  BluetoothDiscoveryFilter df3(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  df3.SetPathloss(45);
  df3.AddUUID(uuid1020);
  df3.AddUUID(uuid1003);
  df3.AddUUID(uuid1004);

  // Having Pathloss and RSSI set in two different filter makes them unequal.
  ASSERT_FALSE(df1.Equals(df3));
}

TEST(BluetoothDiscoveryFilterTest, CopyFrom) {
  BluetoothDiscoveryFilter df1(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);
  df1.SetRSSI(-65);
  df1.AddUUID(uuid1020);
  df1.AddUUID(uuid1003);

  BluetoothDiscoveryFilter df2(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);

  df2.CopyFrom(df1);

  int16_t out_rssi;
  std::set<device::BluetoothUUID> out_uuids;

  // make sure all properties were copied
  df2.GetRSSI(&out_rssi);
  EXPECT_EQ(-65, out_rssi);

  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC,
            df2.GetTransport());

  df2.GetUUIDs(out_uuids);
  EXPECT_TRUE(out_uuids.find(uuid1020) != out_uuids.end());
  EXPECT_TRUE(out_uuids.find(uuid1003) != out_uuids.end());
}

TEST(BluetoothDiscoveryFilterTest, MergeUUIDs) {
  BluetoothDiscoveryFilter df1(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df1.AddUUID(uuid1020);
  df1.AddUUID(uuid1003);

  BluetoothDiscoveryFilter df2(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df2.AddUUID(uuid1020);
  df2.AddUUID(uuid1004);

  scoped_ptr<BluetoothDiscoveryFilter> df3 =
      BluetoothDiscoveryFilter::Merge(&df1, &df2);

  // df3 should contain all uuids from df1 and df2
  std::set<device::BluetoothUUID> out_uuids;
  df3->GetUUIDs(out_uuids);
  EXPECT_TRUE(out_uuids.find(uuid1020) != out_uuids.end());
  EXPECT_TRUE(out_uuids.find(uuid1003) != out_uuids.end());
  EXPECT_TRUE(out_uuids.find(uuid1004) != out_uuids.end());

  // Merging with empty filter would return empty filter
  df3 = BluetoothDiscoveryFilter::Merge(&df1, nullptr);
  df3->GetUUIDs(out_uuids);
  EXPECT_EQ(0UL, out_uuids.size());
  EXPECT_TRUE(df3->IsDefault());
}

TEST(BluetoothDiscoveryFilterTest, MergeProximity) {
  BluetoothDiscoveryFilter df1(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df1.SetRSSI(-50);

  BluetoothDiscoveryFilter df2(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df2.SetRSSI(-70);

  BluetoothDiscoveryFilter df3(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df3.SetPathloss(70);

  BluetoothDiscoveryFilter df4(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);
  df4.SetPathloss(20);

  scoped_ptr<BluetoothDiscoveryFilter> result =
      BluetoothDiscoveryFilter::Merge(&df1, &df2);

  int16_t out_rssi;
  // Merging RSSI should return smaller of both values
  EXPECT_TRUE(result->GetRSSI(&out_rssi));
  EXPECT_EQ(-70, out_rssi);

  uint16_t out_pathloss;
  // Merging RSSI with Pathloss should clear proximity
  result = BluetoothDiscoveryFilter::Merge(&df1, &df3);
  EXPECT_FALSE(result->GetRSSI(&out_rssi));
  EXPECT_FALSE(result->GetPathloss(&out_pathloss));

  // Merging Pathloss should return bigger of both values
  result = BluetoothDiscoveryFilter::Merge(&df3, &df4);
  EXPECT_TRUE(result->GetPathloss(&out_pathloss));
  EXPECT_EQ(70, out_pathloss);
}

TEST(BluetoothDiscoveryFilterTest, MergeTransport) {
  BluetoothDiscoveryFilter df1(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_CLASSIC);

  BluetoothDiscoveryFilter df2(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_LE);

  BluetoothDiscoveryFilter df3(
      BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL);

  scoped_ptr<BluetoothDiscoveryFilter> result =
      BluetoothDiscoveryFilter::Merge(&df1, &df2);

  // Merging LE and CLASSIC should result in both being set
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL,
            result->GetTransport());

  result = BluetoothDiscoveryFilter::Merge(&df1, &df3);
  EXPECT_EQ(BluetoothDiscoveryFilter::Transport::TRANSPORT_DUAL,
            result->GetTransport());

  // Merging with null should alway result with empty filter.
  result = BluetoothDiscoveryFilter::Merge(&df1, nullptr);
  EXPECT_TRUE(result->IsDefault());
}

}  // namespace device
