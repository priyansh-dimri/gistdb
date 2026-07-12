#include "gistdb/execution/filter_operator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../test_utils/mock_operator.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"

namespace gistdb::execution {
namespace {

using gistdb::test_utils::MockOperator;

TEST(FilterOperatorTest, RowsPassingPredicateStaySelected) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(3);
  column.Append(7);
  column.Append(2);
  column.Append(9);

  DataChunk chunk(4);
  chunk.AddColumn(&column);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan,
                                 MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5));
  FilterOperator filter(std::move(child), std::move(predicate));

  std::optional<DataChunk> result = filter.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->IsRowSelected(0));  // 3 > 5 false
  EXPECT_TRUE(result->IsRowSelected(1));   // 7 > 5 true
  EXPECT_FALSE(result->IsRowSelected(2));  // 2 > 5 false
  EXPECT_TRUE(result->IsRowSelected(3));   // 9 > 5 true
}

TEST(FilterOperatorTest, PreExistingDeselectionIsPreserved) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(7);
  column.Append(9);

  DataChunk chunk(2);
  chunk.AddColumn(&column);
  chunk.SetRowSelected(0, false);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  // Both rows would pass this predicate on their own (7 > 5, 9 > 5).
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan,
                                 MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5));
  FilterOperator filter(std::move(child), std::move(predicate));

  std::optional<DataChunk> result = filter.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->IsRowSelected(0));
  EXPECT_TRUE(result->IsRowSelected(1));
}

TEST(FilterOperatorTest, PropagatesEndOfStreamFromChild) {
  auto child = std::make_unique<MockOperator>(std::vector<DataChunk>{});  
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, MakeIntConst(1), MakeIntConst(0));
  FilterOperator filter(std::move(child), std::move(predicate));

  EXPECT_FALSE(filter.GetNext().has_value());
}

TEST(FilterOperatorTest, ProcessesMultipleChunksIndependently) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column1;
  column1.Append(10);
  DataChunk chunk1(1);
  chunk1.AddColumn(&column1);

  gistdb::storage::FixedWidthColumn<std::int32_t> column2;
  column2.Append(1);
  DataChunk chunk2(1);
  chunk2.AddColumn(&column2);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk1));
  chunks.push_back(std::move(chunk2));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan,
                                 MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5));
  FilterOperator filter(std::move(child), std::move(predicate));

  std::optional<DataChunk> first = filter.GetNext();
  ASSERT_TRUE(first.has_value());
  EXPECT_TRUE(first->IsRowSelected(0));  // 10 > 5

  std::optional<DataChunk> second = filter.GetNext();
  ASSERT_TRUE(second.has_value());
  EXPECT_FALSE(second->IsRowSelected(0));  // 1 > 5 is false

  EXPECT_FALSE(filter.GetNext().has_value());  
}

TEST(FilterOperatorTest, RowCountIsUnchangedEvenWhenAllRowsRejected) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(1);
  column.Append(2);
  column.Append(3);

  DataChunk chunk(3);
  chunk.AddColumn(&column);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan,
                                 MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(100));
  FilterOperator filter(std::move(child), std::move(predicate));

  std::optional<DataChunk> result = filter.GetNext();
  ASSERT_TRUE(result.has_value());    
  EXPECT_EQ(result->RowCount(), 3U);  
  EXPECT_EQ(result->CountSelectedRows(), 0U);
}

TEST(FilterOperatorTest, NullPredicateResultExcludesRow) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.AppendNull();
  column.Append(7);

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan,
                                 MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5));
  FilterOperator filter(std::move(child), std::move(predicate));

  std::optional<DataChunk> result = filter.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->IsRowSelected(0)); 
  EXPECT_TRUE(result->IsRowSelected(1));
}

}  // namespace
}  // namespace gistdb::execution