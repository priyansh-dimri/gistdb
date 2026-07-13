#include "gistdb/binder/type_coercion.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

#include "gistdb/binder/ast.hpp"
#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::binder {
namespace {

using gistdb::execution::BinaryOperator;
using gistdb::execution::ExpressionType;
using gistdb::execution::UnaryOperator;

TEST(TypeCoercionTest, IdentifiesComparisonOperators) {
  EXPECT_TRUE(IsComparisonOperator(BinaryOperator::kEqual));
  EXPECT_TRUE(IsComparisonOperator(BinaryOperator::kGreaterThanOrEqual));
  EXPECT_FALSE(IsComparisonOperator(BinaryOperator::kAdd));
  EXPECT_FALSE(IsComparisonOperator(BinaryOperator::kAnd));
}

TEST(TypeCoercionTest, IdentifiesLogicalOperators) {
  EXPECT_TRUE(IsLogicalOperator(BinaryOperator::kAnd));
  EXPECT_TRUE(IsLogicalOperator(BinaryOperator::kOr));
  EXPECT_FALSE(IsLogicalOperator(BinaryOperator::kEqual));
}

TEST(TypeCoercionTest, RequiresFloatPromotionOnlyForMixedIntFloat) {
  EXPECT_TRUE(RequiresFloatPromotion(ExpressionType::kInteger, ExpressionType::kFloat));
  EXPECT_TRUE(RequiresFloatPromotion(ExpressionType::kFloat, ExpressionType::kInteger));
  EXPECT_FALSE(RequiresFloatPromotion(ExpressionType::kInteger, ExpressionType::kInteger));
  EXPECT_FALSE(RequiresFloatPromotion(ExpressionType::kFloat, ExpressionType::kFloat));
}

TEST(TypeCoercionTest, LogicalOpRequiresBooleanOperands) {
  EXPECT_EQ(ResultTypeForBinaryOp(BinaryOperator::kAnd, ExpressionType::kBoolean,
                                  ExpressionType::kBoolean),
            ExpressionType::kBoolean);
  EXPECT_THROW((void)ResultTypeForBinaryOp(BinaryOperator::kAnd, ExpressionType::kInteger,
                                           ExpressionType::kBoolean),
               TypeCoercionException);
}

TEST(TypeCoercionTest, ComparisonAlwaysProducesBoolean) {
  EXPECT_EQ(ResultTypeForBinaryOp(BinaryOperator::kGreaterThan, ExpressionType::kInteger,
                                  ExpressionType::kInteger),
            ExpressionType::kBoolean);
  EXPECT_EQ(ResultTypeForBinaryOp(BinaryOperator::kEqual, ExpressionType::kVarchar,
                                  ExpressionType::kVarchar),
            ExpressionType::kBoolean);
}

TEST(TypeCoercionTest, VarcharCannotMixWithNumericInComparison) {
  EXPECT_THROW((void)ResultTypeForBinaryOp(BinaryOperator::kEqual, ExpressionType::kVarchar,
                                           ExpressionType::kInteger),
               TypeCoercionException);
}

TEST(TypeCoercionTest, VarcharCannotMixWithNumericInArithmetic) {
  EXPECT_THROW((void)ResultTypeForBinaryOp(BinaryOperator::kAdd, ExpressionType::kVarchar,
                                           ExpressionType::kInteger),
               TypeCoercionException);
}

TEST(TypeCoercionTest, VarcharDoesNotSupportArithmeticEvenWithItself) {
  EXPECT_THROW((void)ResultTypeForBinaryOp(BinaryOperator::kAdd, ExpressionType::kVarchar,
                                           ExpressionType::kVarchar),
               TypeCoercionException);
}

TEST(TypeCoercionTest, IntegerArithmeticStaysInteger) {
  EXPECT_EQ(ResultTypeForBinaryOp(BinaryOperator::kAdd, ExpressionType::kInteger,
                                  ExpressionType::kInteger),
            ExpressionType::kInteger);
}

TEST(TypeCoercionTest, MixedIntFloatArithmeticPromotesToFloat) {
  EXPECT_EQ(
      ResultTypeForBinaryOp(BinaryOperator::kAdd, ExpressionType::kInteger, ExpressionType::kFloat),
      ExpressionType::kFloat);
}

TEST(TypeCoercionTest, NotRequiresBooleanOperand) {
  EXPECT_EQ(ResultTypeForUnaryOp(UnaryOperator::kNot, ExpressionType::kBoolean),
            ExpressionType::kBoolean);
  EXPECT_THROW((void)ResultTypeForUnaryOp(UnaryOperator::kNot, ExpressionType::kInteger),
               TypeCoercionException);
}

TEST(TypeCoercionTest, BindConstNarrowsInRangeIntegerLiteral) {
  auto expr = BindConst(ConstNode{std::int64_t{42}});
  const auto& const_node = std::get<gistdb::execution::ConstNode>(expr->node);
  EXPECT_EQ(std::get<std::int32_t>(const_node.value), 42);
}

TEST(TypeCoercionTest, BindConstThrowsOnOutOfInt32RangeLiteral) {
  auto value = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1;
  EXPECT_THROW((void)BindConst(ConstNode{value}), TypeCoercionException);
}

TEST(TypeCoercionTest, BindConstNarrowsDoubleToFloat) {
  auto expr = BindConst(ConstNode{3.5});
  const auto& const_node = std::get<gistdb::execution::ConstNode>(expr->node);
  EXPECT_FLOAT_EQ(std::get<float>(const_node.value), 3.5F);
}

TEST(TypeCoercionTest, BindConstPreservesStringLiteral) {
  auto expr = BindConst(ConstNode{std::string("hello")});
  const auto& const_node = std::get<gistdb::execution::ConstNode>(expr->node);
  EXPECT_EQ(std::get<std::string>(const_node.value), "hello");
}

TEST(TypeCoercionTest, BindConstThrowsOnNullLiteralUntilConstNodeSupportsIt) {
  EXPECT_THROW((void)BindConst(ConstNode{NullLiteral{}}), TypeCoercionException);
}

}  // namespace
}  // namespace gistdb::binder