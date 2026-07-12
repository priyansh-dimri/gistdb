#include "gistdb/execution/aggregate_accumulator.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace gistdb::execution {
namespace {

TEST(CountStarAccumulatorTest, CountsEveryAddUnconditionally) {
  CountStarAccumulator acc;
  acc.Add();
  acc.Add();
  acc.Add();
  EXPECT_EQ(acc.Count(), 3);
}

TEST(CountStarAccumulatorTest, StartsAtZero) {
  CountStarAccumulator acc;
  EXPECT_EQ(acc.Count(), 0);
}

TEST(CountAccumulatorTest, CountsOnlyNonNullValues) {
  CountAccumulator acc;
  acc.Add(false);
  acc.Add(true);
  acc.Add(false);
  acc.Add(true);
  EXPECT_EQ(acc.Count(), 2);
}

TEST(SumIntAccumulatorTest, SumsOnlyNonNullValues) {
  SumIntAccumulator acc;
  acc.Add(10, false);
  acc.Add(0, true);
  acc.Add(5, false);
  EXPECT_TRUE(acc.HasValues());
  EXPECT_EQ(acc.Sum(), 15);
}

TEST(SumIntAccumulatorTest, HasNoValuesWhenAllInputsAreNull) {
  SumIntAccumulator acc;
  acc.Add(0, true);
  acc.Add(0, true);
  EXPECT_FALSE(acc.HasValues());
}

TEST(SumIntAccumulatorTest, WidensToInt64ToAvoidOverflow) {
  SumIntAccumulator acc;
  acc.Add(2'000'000'000, false);
  acc.Add(2'000'000'000, false);
  EXPECT_EQ(acc.Sum(), 4'000'000'000LL);
}

TEST(SumFloatAccumulatorTest, SumsOnlyNonNullValues) {
  SumFloatAccumulator acc;
  acc.Add(1.5F, false);
  acc.Add(100.0F, true);
  acc.Add(2.5F, false);
  EXPECT_TRUE(acc.HasValues());
  EXPECT_DOUBLE_EQ(acc.Sum(), 4.0);
}

TEST(AvgIntAccumulatorTest, ComputesAverageOfNonNullValuesOnly) {
  AvgIntAccumulator acc;
  acc.Add(10, false);
  acc.Add(0, true);
  acc.Add(20, false);
  ASSERT_TRUE(acc.HasValues());
  EXPECT_DOUBLE_EQ(acc.Average(), 15.0);
}

TEST(AvgIntAccumulatorTest, HasNoValuesWhenAllInputsAreNull) {
  AvgIntAccumulator acc;
  acc.Add(0, true);
  EXPECT_FALSE(acc.HasValues());
}

TEST(AvgIntAccumulatorTest, AverageIsNotTruncatedToAnInteger) {
  AvgIntAccumulator acc;
  acc.Add(1, false);
  acc.Add(2, false);
  EXPECT_DOUBLE_EQ(acc.Average(), 1.5);
}

TEST(AvgFloatAccumulatorTest, ComputesAverageOfNonNullValuesOnly) {
  AvgFloatAccumulator acc;
  acc.Add(1.0F, false);
  acc.Add(3.0F, false);
  acc.Add(100.0F, true);
  ASSERT_TRUE(acc.HasValues());
  EXPECT_DOUBLE_EQ(acc.Average(), 2.0);
}

TEST(MinMaxAccumulatorIntTest, TracksMinAndMaxSkippingNulls) {
  MinMaxAccumulator<std::int32_t> acc;
  acc.Add(10, false);
  acc.Add(-5, false);
  acc.Add(999, true);
  acc.Add(3, false);
  ASSERT_TRUE(acc.HasValues());
  EXPECT_EQ(acc.Min(), -5);
  EXPECT_EQ(acc.Max(), 10);
}

TEST(MinMaxAccumulatorIntTest, HasNoValuesWhenAllInputsAreNull) {
  MinMaxAccumulator<std::int32_t> acc;
  acc.Add(0, true);
  EXPECT_FALSE(acc.HasValues());
}

TEST(MinMaxAccumulatorFloatTest, TracksMinAndMaxSkippingNulls) {
  MinMaxAccumulator<float> acc;
  acc.Add(1.5F, false);
  acc.Add(-2.5F, false);
  acc.Add(0.0F, true);
  ASSERT_TRUE(acc.HasValues());
  EXPECT_FLOAT_EQ(acc.Min(), -2.5F);
  EXPECT_FLOAT_EQ(acc.Max(), 1.5F);
}

TEST(MinMaxVarcharAccumulatorTest, TracksLexicographicMinAndMaxSkippingNulls) {
  MinMaxVarcharAccumulator acc;
  acc.Add("banana", false);
  acc.Add("apple", false);
  acc.Add("zzz_null_placeholder", true);
  acc.Add("cherry", false);
  ASSERT_TRUE(acc.HasValues());
  EXPECT_EQ(acc.Min(), "apple");
  EXPECT_EQ(acc.Max(), "cherry");
}

TEST(MinMaxVarcharAccumulatorTest, PreservesFullStringBeyondZoneMapPrefixLength) {
  MinMaxVarcharAccumulator acc;
  acc.Add("abcdefghijklmnop", false);
  ASSERT_TRUE(acc.HasValues());
  EXPECT_EQ(acc.Min(), "abcdefghijklmnop");
  EXPECT_EQ(acc.Max(), "abcdefghijklmnop");
}

TEST(MinMaxVarcharAccumulatorTest, HasNoValuesWhenAllInputsAreNull) {
  MinMaxVarcharAccumulator acc;
  acc.Add("x", true);
  EXPECT_FALSE(acc.HasValues());
}

}  // namespace
}  // namespace gistdb::execution