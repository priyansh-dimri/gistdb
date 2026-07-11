#include "gistdb/execution/bound_expression.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace gistdb::execution {
namespace {

TEST(BoundExpressionTest, ColumnRefResultTypeMatchesReferencedColumnType) {
  auto expr = MakeColumnRef(1, 2, gistdb::TypeId::kInteger);
  EXPECT_EQ(expr->ResultType(), ExpressionType::kInteger);
  ASSERT_TRUE(std::holds_alternative<BoundColumnRef>(expr->node));
  const auto& ref = std::get<BoundColumnRef>(expr->node);
  EXPECT_EQ(ref.table_id, 1U);
  EXPECT_EQ(ref.ordinal, 2U);
}

TEST(BoundExpressionTest, IntConstResultTypeIsInteger) {
  auto expr = MakeIntConst(42);
  EXPECT_EQ(expr->ResultType(), ExpressionType::kInteger);
  ASSERT_TRUE(std::holds_alternative<ConstNode>(expr->node));
  EXPECT_EQ(std::get<std::int32_t>(std::get<ConstNode>(expr->node).value), 42);
}

TEST(BoundExpressionTest, FloatConstResultTypeIsFloat) {
  auto expr = MakeFloatConst(3.5F);
  EXPECT_EQ(expr->ResultType(), ExpressionType::kFloat);
}

TEST(BoundExpressionTest, StringConstResultTypeIsVarchar) {
  auto expr = MakeStringConst("hello");
  EXPECT_EQ(expr->ResultType(), ExpressionType::kVarchar);
  EXPECT_EQ(std::get<std::string>(std::get<ConstNode>(expr->node).value), "hello");
}

TEST(BoundExpressionTest, ArithmeticOpReportsItsStoredResultType) {
  auto expr = MakeArithmeticOp(BinaryOperator::kAdd, MakeIntConst(1), MakeIntConst(2),
                               ExpressionType::kInteger);
  EXPECT_EQ(expr->ResultType(), ExpressionType::kInteger);

  // INTEGER + FLOAT should promote to FLOAT
  auto promoted = MakeArithmeticOp(BinaryOperator::kAdd, MakeIntConst(1), MakeFloatConst(2.5F),
                                   ExpressionType::kFloat);
  EXPECT_EQ(promoted->ResultType(), ExpressionType::kFloat);
}

TEST(BoundExpressionTest, ComparisonOpAlwaysReportsBoolean) {
  auto expr = MakeBooleanOp(BinaryOperator::kGreaterThan, MakeIntConst(5), MakeIntConst(3));
  EXPECT_EQ(expr->ResultType(), ExpressionType::kBoolean);
  ASSERT_TRUE(std::holds_alternative<BinaryOpNode>(expr->node));
  EXPECT_EQ(std::get<BinaryOpNode>(expr->node).op, BinaryOperator::kGreaterThan);
}

TEST(BoundExpressionTest, LogicalOpAlwaysReportsBoolean) {
  auto lhs = MakeBooleanOp(BinaryOperator::kGreaterThan, MakeIntConst(5), MakeIntConst(3));
  auto rhs = MakeBooleanOp(BinaryOperator::kLessThan, MakeIntConst(1), MakeIntConst(2));
  auto expr = MakeBooleanOp(BinaryOperator::kAnd, std::move(lhs), std::move(rhs));
  EXPECT_EQ(expr->ResultType(), ExpressionType::kBoolean);
}

TEST(BoundExpressionTest, NotAlwaysReportsBoolean) {
  auto inner = MakeBooleanOp(BinaryOperator::kEqual, MakeIntConst(1), MakeIntConst(1));
  auto expr = MakeNot(std::move(inner));
  EXPECT_EQ(expr->ResultType(), ExpressionType::kBoolean);
  ASSERT_TRUE(std::holds_alternative<UnaryOpNode>(expr->node));
  EXPECT_EQ(std::get<UnaryOpNode>(expr->node).op, UnaryOperator::kNot);
}

TEST(BoundExpressionTest, RecursiveTreeNavigatesCorrectly) {
  // (col_0 + 5) > 10
  auto tree = MakeBooleanOp(
      BinaryOperator::kGreaterThan,
      MakeArithmeticOp(BinaryOperator::kAdd, MakeColumnRef(0, 0, gistdb::TypeId::kInteger),
                       MakeIntConst(5), ExpressionType::kInteger),
      MakeIntConst(10));

  ASSERT_TRUE(std::holds_alternative<BinaryOpNode>(tree->node));
  const auto& top = std::get<BinaryOpNode>(tree->node);
  EXPECT_EQ(top.op, BinaryOperator::kGreaterThan);

  ASSERT_TRUE(std::holds_alternative<BinaryOpNode>(top.left->node));
  const auto& inner = std::get<BinaryOpNode>(top.left->node);
  EXPECT_EQ(inner.op, BinaryOperator::kAdd);
  ASSERT_TRUE(std::holds_alternative<BoundColumnRef>(inner.left->node));
  EXPECT_EQ(std::get<BoundColumnRef>(inner.left->node).ordinal, 0U);

  ASSERT_TRUE(std::holds_alternative<ConstNode>(top.right->node));
  EXPECT_EQ(std::get<std::int32_t>(std::get<ConstNode>(top.right->node).value), 10);
}

TEST(ToExpressionTypeTest, ConvertsEachStorableTypeCorrectly) {
  EXPECT_EQ(ToExpressionType(gistdb::TypeId::kInteger), ExpressionType::kInteger);
  EXPECT_EQ(ToExpressionType(gistdb::TypeId::kFloat), ExpressionType::kFloat);
  EXPECT_EQ(ToExpressionType(gistdb::TypeId::kVarchar), ExpressionType::kVarchar);
}

}  // namespace
}  // namespace gistdb::execution