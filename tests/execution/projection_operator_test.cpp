#include "gistdb/execution/projection_operator.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../test_utils/mock_operator.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"

namespace gistdb::execution {
namespace {

using gistdb::test_utils::MockOperator;

TEST(ProjectionOperatorTest, ProjectsBareColumnReference) {
  gistdb::storage::FixedWidthColumn<std::int32_t> col;
  col.Append(10);
  col.Append(20);
  DataChunk chunk(2);
  chunk.AddColumn(&col);

  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  std::vector<std::unique_ptr<BoundExpression>> exprs;
  exprs.push_back(MakeColumnRef(0, 0, gistdb::TypeId::kInteger));
  ProjectionOperator projection(std::move(child), std::move(exprs));

  auto result = projection.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* out =
      std::get<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(out->GetValue(0), 10);
  EXPECT_EQ(out->GetValue(1), 20);
}

TEST(ProjectionOperatorTest, ProjectsArithmeticExpression) {
  gistdb::storage::FixedWidthColumn<std::int32_t> col;
  col.Append(5);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  std::vector<std::unique_ptr<BoundExpression>> exprs;
  exprs.push_back(MakeArithmeticOp(BinaryOperator::kAdd,
                                   MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(100),
                                   ExpressionType::kInteger));
  ProjectionOperator projection(std::move(child), std::move(exprs));

  auto result = projection.GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* out =
      std::get<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(out->GetValue(0), 105);
}

TEST(ProjectionOperatorTest, PreservesSelectionVectorFromChild) {
  gistdb::storage::FixedWidthColumn<std::int32_t> col;
  col.Append(1);
  col.Append(2);
  DataChunk chunk(2);
  chunk.AddColumn(&col);
  chunk.SetRowSelected(0, false);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  std::vector<std::unique_ptr<BoundExpression>> exprs;
  exprs.push_back(MakeColumnRef(0, 0, gistdb::TypeId::kInteger));
  ProjectionOperator projection(std::move(child), std::move(exprs));

  auto result = projection.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->IsRowSelected(0));
  EXPECT_TRUE(result->IsRowSelected(1));
}

TEST(ProjectionOperatorTest, MultipleExpressionsProduceMultipleOutputColumns) {
  gistdb::storage::FixedWidthColumn<std::int32_t> col;
  col.Append(7);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  std::vector<std::unique_ptr<BoundExpression>> exprs;
  exprs.push_back(MakeColumnRef(0, 0, gistdb::TypeId::kInteger));
  exprs.push_back(MakeIntConst(42));
  ProjectionOperator projection(std::move(child), std::move(exprs));

  auto result = projection.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->NumColumns(), 2U);
}

TEST(ProjectionOperatorTest, ProjectingBareBooleanExpressionThrows) {
  gistdb::storage::FixedWidthColumn<std::int32_t> col;
  col.Append(5);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  std::vector<DataChunk> chunks;
  chunks.push_back(std::move(chunk));
  auto child = std::make_unique<MockOperator>(std::move(chunks));

  std::vector<std::unique_ptr<BoundExpression>> exprs;
  exprs.push_back(MakeBooleanOp(BinaryOperator::kGreaterThan,
                                MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(1)));
  ProjectionOperator projection(std::move(child), std::move(exprs));

  EXPECT_THROW((void)projection.GetNext(), std::runtime_error);
}

TEST(ProjectionOperatorTest, PropagatesEndOfStreamFromChild) {
  auto child = std::make_unique<MockOperator>(std::vector<DataChunk>{});
  std::vector<std::unique_ptr<BoundExpression>> exprs;
  exprs.push_back(MakeIntConst(1));
  ProjectionOperator projection(std::move(child), std::move(exprs));

  EXPECT_FALSE(projection.GetNext().has_value());
}

}  // namespace
}  // namespace gistdb::execution