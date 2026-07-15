#include "gistdb/execution/aggregation_operator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../test_utils/mock_operator.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {
namespace {

using gistdb::test_utils::MockOperator;
using IntCol = gistdb::storage::FixedWidthColumn<std::int32_t>;
using FloatCol = gistdb::storage::FixedWidthColumn<float>;
using VarcharCol = gistdb::storage::VarcharColumn;

TEST(AggregationOperatorTest, CountStarCountsEveryRowRegardlessOfNulls) {
  IntCol col;
  col.Append(1);
  col.AppendNull();
  col.Append(3);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kCountStar, std::nullopt, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* count_col = std::get<const IntCol*>(result->Column(0));
  EXPECT_EQ(count_col->GetValue(0), 3);
  EXPECT_FALSE(agg.GetNext().has_value());
}

TEST(AggregationOperatorTest, CountColumnSkipsNulls) {
  IntCol col;
  col.Append(1);
  col.AppendNull();
  col.Append(3);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kCount, 0U, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* count_col = std::get<const IntCol*>(result->Column(0));
  EXPECT_EQ(count_col->GetValue(0), 2);
}

TEST(AggregationOperatorTest, SumIntSkipsNullsAndSumsCorrectly) {
  IntCol col;
  col.Append(10);
  col.AppendNull();
  col.Append(20);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kSum, 0U, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* sum_col = std::get<const IntCol*>(result->Column(0));
  EXPECT_EQ(sum_col->GetValue(0), 30);
}

TEST(AggregationOperatorTest, AvgComputesCorrectAverageIgnoringNulls) {
  IntCol col;
  col.Append(10);
  col.AppendNull();
  col.Append(20);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kAvg, 0U, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* avg_col = std::get<const FloatCol*>(result->Column(0));
  EXPECT_FLOAT_EQ(avg_col->GetValue(0), 15.0F);
}

// Directly catches the MIN-vs-MAX mix-up: MIN and MAX on the same column
// must produce genuinely different values, not both silently return MIN.
TEST(AggregationOperatorTest, MinAndMaxOnSameIntColumnProduceDifferentResults) {
  IntCol col;
  col.Append(10);
  col.Append(3);
  col.Append(7);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kMin, 0U, gistdb::TypeId::kInteger},
       AggregateSpec{AggregateFunctionKind::kMax, 0U, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* min_col = std::get<const IntCol*>(result->Column(0));
  const auto* max_col = std::get<const IntCol*>(result->Column(1));
  EXPECT_EQ(min_col->GetValue(0), 3);
  EXPECT_EQ(max_col->GetValue(0), 10);
}

TEST(AggregationOperatorTest, MinAndMaxOnVarcharColumn) {
  VarcharCol col;
  col.Append("banana");
  col.Append("apple");
  col.Append("cherry");
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kMin, 0U, gistdb::TypeId::kVarchar},
       AggregateSpec{AggregateFunctionKind::kMax, 0U, gistdb::TypeId::kVarchar}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* min_col = std::get<const VarcharCol*>(result->Column(0));
  const auto* max_col = std::get<const VarcharCol*>(result->Column(1));
  EXPECT_EQ(min_col->GetValue(0), "apple");
  EXPECT_EQ(max_col->GetValue(0), "cherry");
}

TEST(AggregationOperatorTest, NoGroupByProducesSingleRowEvenOverEmptyInput) {
  auto child = std::make_unique<MockOperator>(std::vector<DataChunk>{});
  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kCountStar, std::nullopt, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 1U);
  const auto* count_col = std::get<const IntCol*>(result->Column(0));
  EXPECT_EQ(count_col->GetValue(0), 0);
}

// Also verifies output column order: group-by column first, then the
// aggregate result -- regardless of the two having different underlying
// storage types (varchar vs int).
TEST(AggregationOperatorTest, GroupByProducesOneRowPerDistinctKeyInLogicalColumnOrder) {
  VarcharCol category;
  category.Append("a");
  category.Append("b");
  category.Append("a");
  IntCol value;
  value.Append(10);
  value.Append(20);
  value.Append(30);
  DataChunk chunk(3);
  chunk.AddColumn(&category);
  chunk.AddColumn(&value);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {0}, {gistdb::TypeId::kVarchar},
      {AggregateSpec{AggregateFunctionKind::kSum, 1U, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RowCount(), 2U);

  const auto* category_col = std::get<const VarcharCol*>(result->Column(0));
  const auto* sum_col = std::get<const IntCol*>(result->Column(1));
  EXPECT_EQ(category_col->GetValue(0), "a");
  EXPECT_EQ(sum_col->GetValue(0), 40);
  EXPECT_EQ(category_col->GetValue(1), "b");
  EXPECT_EQ(sum_col->GetValue(1), 20);
}

TEST(AggregationOperatorTest, UnselectedRowsAreExcludedFromAccumulation) {
  IntCol col;
  col.Append(10);
  col.Append(20);
  col.Append(30);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  chunk.SetRowSelected(1, false);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {}, {},
      {AggregateSpec{AggregateFunctionKind::kCountStar, std::nullopt, gistdb::TypeId::kInteger}});
  auto result = agg.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* count_col = std::get<const IntCol*>(result->Column(0));
  EXPECT_EQ(count_col->GetValue(0), 2);
}

TEST(AggregationOperatorTest, ChunkedEmissionAcrossMultipleGetNextCallsForManyGroups) {
  const std::uint32_t total_groups = gistdb::kVectorSize + 5;
  IntCol first_batch;
  for (std::uint32_t i = 0; i < gistdb::kVectorSize; ++i) {
    first_batch.Append(static_cast<std::int32_t>(i));
  }
  DataChunk chunk1(gistdb::kVectorSize);
  chunk1.AddColumn(&first_batch);

  IntCol second_batch;
  for (std::uint32_t i = gistdb::kVectorSize; i < total_groups; ++i) {
    second_batch.Append(static_cast<std::int32_t>(i));
  }
  DataChunk chunk2(5);
  chunk2.AddColumn(&second_batch);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk1));
  chunks.push_back(std::move(chunk2));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  AggregationOperator agg(
      std::move(child), {0}, {gistdb::TypeId::kInteger},
      {AggregateSpec{AggregateFunctionKind::kCountStar, std::nullopt, gistdb::TypeId::kInteger}});

  auto first = agg.GetNext();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->RowCount(), gistdb::kVectorSize);

  auto second = agg.GetNext();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->RowCount(), 5U);

  EXPECT_FALSE(agg.GetNext().has_value());
}

}  // namespace
}  // namespace gistdb::execution