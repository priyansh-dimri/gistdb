#include <gtest/gtest.h>

#include "gistdb/constants.hpp"
#include "gistdb/storage/zone_map.hpp"

namespace gistdb::storage {
namespace {

TEST(ZoneMapTest, StartsWithNoValues) {
  ZoneMap<std::int32_t> zone_map;
  EXPECT_FALSE(zone_map.HasValues());
}

TEST(ZoneMapTest, FirstUpdateSetsMinAndMax) {
  ZoneMap<std::int32_t> zone_map;
  zone_map.Update(42);
  EXPECT_TRUE(zone_map.HasValues());
  EXPECT_EQ(zone_map.Min(), 42);
  EXPECT_EQ(zone_map.Max(), 42);
}

TEST(ZoneMapTest, SubsequentUpdatesExpandRange) {
  ZoneMap<std::int32_t> zone_map;
  zone_map.Update(10);
  zone_map.Update(30);
  zone_map.Update(5);
  zone_map.Update(20);
  EXPECT_EQ(zone_map.Min(), 5);
  EXPECT_EQ(zone_map.Max(), 30);
}

TEST(ZoneMapTest, WorksForFloatToo) {
  ZoneMap<float> zone_map;
  zone_map.Update(3.5F);
  zone_map.Update(-1.2F);
  zone_map.Update(2.0F);
  EXPECT_FLOAT_EQ(zone_map.Min(), -1.2F);
  EXPECT_FLOAT_EQ(zone_map.Max(), 3.5F);
}

TEST(VarcharZoneMapTest, StartsWithNoValues) {
  VarcharZoneMap zone_map;
  EXPECT_FALSE(zone_map.HasValues());
}

TEST(VarcharZoneMapTest, FirstUpdateSetsMinAndMaxPrefix) {
  VarcharZoneMap zone_map;
  zone_map.Update("banana");
  EXPECT_EQ(zone_map.MinPrefix(), "banana");
  EXPECT_EQ(zone_map.MaxPrefix(), "banana");
}

TEST(VarcharZoneMapTest, SubsequentUpdatesExpandLexicographicRange) {
  VarcharZoneMap zone_map;
  zone_map.Update("banana");
  zone_map.Update("apple");
  zone_map.Update("cherry");
  EXPECT_EQ(zone_map.MinPrefix(), "apple");
  EXPECT_EQ(zone_map.MaxPrefix(), "cherry");
}

TEST(VarcharZoneMapTest, PrefixIsTruncatedToZoneMapPrefixLength) {
  VarcharZoneMap zone_map;
  zone_map.Update(
      "abcdefghijklmnop"); // longer than the prefix length
  EXPECT_EQ(zone_map.MinPrefix().size(), kZoneMapPrefixLength);
  EXPECT_EQ(zone_map.MinPrefix(), "abcdefgh");
}

TEST(VarcharZoneMapTest, ShorterTruePrefixComparesAsSmaller) {
  VarcharZoneMap zone_map;
  zone_map.Update("app");
  zone_map.Update("apple");
  EXPECT_EQ(zone_map.MinPrefix(), "app");
  EXPECT_EQ(zone_map.MaxPrefix(), "apple");
}

} // namespace
} // namespace gistdb::storage