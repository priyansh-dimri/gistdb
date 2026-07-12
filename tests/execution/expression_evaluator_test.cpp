#include "gistdb/execution/expression_evaluator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {
namespace {

TEST(ExpressionEvaluatorTest, ColumnRefCopiesValuesAndValidity) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(10);
  column.AppendNull();
  column.Append(30);

  DataChunk chunk(3);
  chunk.AddColumn(&column);

  auto expr = MakeColumnRef(0, 0, gistdb::TypeId::kInteger);
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& int_result = std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result);
  EXPECT_EQ(int_result.GetValue(0), 10);
  EXPECT_TRUE(int_result.IsNull(1));
  EXPECT_EQ(int_result.GetValue(2), 30);
}

TEST(ExpressionEvaluatorTest, IntConstBroadcastsAcrossAllRows) {
  DataChunk chunk(3);
  auto expr = MakeIntConst(7);
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& int_result = std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result);
  for (std::uint32_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(int_result.IsNull(i));
    EXPECT_EQ(int_result.GetValue(i), 7);
  }
}

TEST(ExpressionEvaluatorTest, StringConstBroadcastsAcrossAllRows) {
  DataChunk chunk(2);
  auto expr = MakeStringConst("hi");
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& str_result = std::get<gistdb::storage::VarcharColumn>(result);
  EXPECT_EQ(str_result.GetValue(0), "hi");
  EXPECT_EQ(str_result.GetValue(1), "hi");
}

TEST(ExpressionEvaluatorTest, IntegerAdditionComputesCorrectValues) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(1);
  column.Append(2);
  column.Append(3);

  DataChunk chunk(3);
  chunk.AddColumn(&column);

  auto expr = MakeArithmeticOp(BinaryOperator::kAdd, MakeColumnRef(0, 0, gistdb::TypeId::kInteger),
                               MakeIntConst(10), ExpressionType::kInteger);
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& int_result = std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result);
  EXPECT_EQ(int_result.GetValue(0), 11);
  EXPECT_EQ(int_result.GetValue(1), 12);
  EXPECT_EQ(int_result.GetValue(2), 13);
}

TEST(ExpressionEvaluatorTest, IntPlusFloatPromotesToFloat) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(1);

  DataChunk chunk(1);
  chunk.AddColumn(&column);

  auto expr = MakeArithmeticOp(BinaryOperator::kAdd, MakeColumnRef(0, 0, gistdb::TypeId::kInteger),
                               MakeFloatConst(2.5F), ExpressionType::kFloat);
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& float_result = std::get<gistdb::storage::FixedWidthColumn<float>>(result);
  EXPECT_FLOAT_EQ(float_result.GetValue(0), 3.5F);
}

TEST(ExpressionEvaluatorTest, ArithmeticPropagatesNullFromEitherOperand) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(1);
  column.AppendNull();

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  auto expr = MakeArithmeticOp(BinaryOperator::kAdd, MakeColumnRef(0, 0, gistdb::TypeId::kInteger),
                               MakeIntConst(10), ExpressionType::kInteger);
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& int_result = std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result);
  EXPECT_FALSE(int_result.IsNull(0));
  EXPECT_TRUE(int_result.IsNull(1));
}

TEST(ExpressionEvaluatorTest, IntegerDivideByZeroBecomesNullNotUndefinedBehavior) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(10);

  DataChunk chunk(1);
  chunk.AddColumn(&column);

  auto expr =
      MakeArithmeticOp(BinaryOperator::kDivide, MakeColumnRef(0, 0, gistdb::TypeId::kInteger),
                       MakeIntConst(0), ExpressionType::kInteger);
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& int_result = std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result);
  EXPECT_TRUE(int_result.IsNull(0));
}

TEST(ExpressionEvaluatorTest, IntegerComparisonProducesCorrectBooleanValues) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(3);
  column.Append(7);

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  auto expr = MakeBooleanOp(BinaryOperator::kGreaterThan,
                            MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5));
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& bool_result = std::get<BooleanResult>(result);
  EXPECT_FALSE(bool_result.values.IsValid(0));  // 3 > 5 is false
  EXPECT_TRUE(bool_result.values.IsValid(1));   // 7 > 5 is true
  EXPECT_TRUE(bool_result.validity.IsValid(0));
  EXPECT_TRUE(bool_result.validity.IsValid(1));
}

TEST(ExpressionEvaluatorTest, ComparisonPropagatesNullFromEitherOperand) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.AppendNull();
  column.Append(7);

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  auto expr = MakeBooleanOp(BinaryOperator::kGreaterThan,
                            MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5));
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& bool_result = std::get<BooleanResult>(result);
  EXPECT_FALSE(bool_result.validity.IsValid(0));
  EXPECT_TRUE(bool_result.validity.IsValid(1));
}

TEST(ExpressionEvaluatorTest, VarcharComparisonIsLexicographic) {
  gistdb::storage::VarcharColumn column;
  column.Append("apple");
  column.Append("cherry");

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  auto expr =
      MakeBooleanOp(BinaryOperator::kLessThan, MakeColumnRef(0, 0, gistdb::TypeId::kVarchar),
                    MakeStringConst("banana"));
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& bool_result = std::get<BooleanResult>(result);
  EXPECT_TRUE(bool_result.values.IsValid(0));   // "apple" < "banana"
  EXPECT_FALSE(bool_result.values.IsValid(1));  // "cherry" < "banana" is false
}

TEST(ExpressionEvaluatorTest, LogicalAndCombinesTwoComparisons) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(7);   // > 5 and < 10 is true
  column.Append(3);   // > 5 is false
  column.Append(15);  // < 10 is false

  DataChunk chunk(3);
  chunk.AddColumn(&column);

  auto expr =
      MakeBooleanOp(BinaryOperator::kAnd,
                    MakeBooleanOp(BinaryOperator::kGreaterThan,
                                  MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5)),
                    MakeBooleanOp(BinaryOperator::kLessThan,
                                  MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(10)));
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& bool_result = std::get<BooleanResult>(result);
  EXPECT_TRUE(bool_result.values.IsValid(0));
  EXPECT_FALSE(bool_result.values.IsValid(1));
  EXPECT_FALSE(bool_result.values.IsValid(2));
}

TEST(ExpressionEvaluatorTest, NotNegatesValueAndPreservesValidity) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(7);
  column.AppendNull();

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  auto expr =
      MakeNot(MakeBooleanOp(BinaryOperator::kGreaterThan,
                            MakeColumnRef(0, 0, gistdb::TypeId::kInteger), MakeIntConst(5)));
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& bool_result = std::get<BooleanResult>(result);
  EXPECT_FALSE(bool_result.values.IsValid(0));  // NOT(7 > 5) = NOT(true) = false
  EXPECT_TRUE(bool_result.validity.IsValid(0));
  EXPECT_FALSE(bool_result.validity.IsValid(1));
}

TEST(ExpressionEvaluatorTest, NestedTreeEvaluatesEndToEnd) {
  // (col_0 + 5) > 10
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(3);   // 3+5=8, 8>10 false
  column.Append(10);  // 10+5=15, 15>10 true

  DataChunk chunk(2);
  chunk.AddColumn(&column);

  auto expr = MakeBooleanOp(
      BinaryOperator::kGreaterThan,
      MakeArithmeticOp(BinaryOperator::kAdd, MakeColumnRef(0, 0, gistdb::TypeId::kInteger),
                       MakeIntConst(5), ExpressionType::kInteger),
      MakeIntConst(10));
  EvaluationResult result = ExpressionEvaluator::Evaluate(*expr, chunk);

  const auto& bool_result = std::get<BooleanResult>(result);
  EXPECT_FALSE(bool_result.values.IsValid(0));
  EXPECT_TRUE(bool_result.values.IsValid(1));
}

}  // namespace
}  // namespace gistdb::execution