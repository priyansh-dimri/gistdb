#include "gistdb/binder/ast.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gistdb::binder {
namespace {

TEST(AstTest, MakeColumnRefWithoutQualifier) {
  auto expr = MakeColumnRef(std::nullopt, "age");
  const auto& ref = std::get<ColumnRefNode>(expr->node);
  EXPECT_FALSE(ref.table_qualifier.has_value());
  EXPECT_EQ(ref.column_name, "age");
}

TEST(AstTest, MakeColumnRefWithQualifier) {
  auto expr = MakeColumnRef("u", "age");
  const auto& ref = std::get<ColumnRefNode>(expr->node);
  ASSERT_TRUE(ref.table_qualifier.has_value());
  EXPECT_EQ(*ref.table_qualifier, "u");
  EXPECT_EQ(ref.column_name, "age");
}

TEST(AstTest, MakeNullConstHoldsNullLiteral) {
  auto expr = MakeNullConst();
  const auto& node = std::get<ConstNode>(expr->node);
  EXPECT_TRUE(std::holds_alternative<NullLiteral>(node.value));
}

TEST(AstTest, MakeIntConstStoresWidenedInt64) {
  auto expr = MakeIntConst(9'999'999'999LL);  // exceeds int32_t range
  const auto& node = std::get<ConstNode>(expr->node);
  EXPECT_EQ(std::get<std::int64_t>(node.value), 9'999'999'999LL);
}

TEST(AstTest, MakeFloatConstStoresDouble) {
  auto expr = MakeFloatConst(3.5);
  const auto& node = std::get<ConstNode>(expr->node);
  EXPECT_DOUBLE_EQ(std::get<double>(node.value), 3.5);
}

TEST(AstTest, MakeStringConstStoresValue) {
  auto expr = MakeStringConst("hello");
  const auto& node = std::get<ConstNode>(expr->node);
  EXPECT_EQ(std::get<std::string>(node.value), "hello");
}

TEST(AstTest, MakeBinaryOpStoresOperatorAndBothOperands) {
  auto expr = MakeBinaryOp(BinaryOperator::kAdd, MakeIntConst(1), MakeIntConst(2));
  const auto& node = std::get<BinaryOpNode>(expr->node);
  EXPECT_EQ(node.op, BinaryOperator::kAdd);
  EXPECT_EQ(std::get<std::int64_t>(std::get<ConstNode>(node.left->node).value), 1);
  EXPECT_EQ(std::get<std::int64_t>(std::get<ConstNode>(node.right->node).value), 2);
}

TEST(AstTest, MakeUnaryOpStoresOperatorAndOperand) {
  auto expr = MakeUnaryOp(UnaryOperator::kNot, MakeIntConst(1));
  const auto& node = std::get<UnaryOpNode>(expr->node);
  EXPECT_EQ(node.op, UnaryOperator::kNot);
  EXPECT_EQ(std::get<std::int64_t>(std::get<ConstNode>(node.operand->node).value), 1);
}

TEST(AstTest, MakeFunctionCallStoresNameAndArgs) {
  std::vector<std::unique_ptr<RawExpression>> args;
  args.push_back(MakeColumnRef(std::nullopt, "price"));
  auto expr = MakeFunctionCall("sum", std::move(args));
  const auto& node = std::get<FunctionCallNode>(expr->node);
  EXPECT_EQ(node.name, "sum");
  EXPECT_FALSE(node.is_star_arg);
  ASSERT_EQ(node.args.size(), 1U);
  EXPECT_EQ(std::get<ColumnRefNode>(node.args[0]->node).column_name, "price");
}

TEST(AstTest, MakeFunctionCallSupportsStarArgWithNoArgs) {
  auto expr = MakeFunctionCall("count", {}, true);
  const auto& node = std::get<FunctionCallNode>(expr->node);
  EXPECT_EQ(node.name, "count");
  EXPECT_TRUE(node.is_star_arg);
  EXPECT_TRUE(node.args.empty());
}

TEST(AstTest, MakeBaseTableRefStoresNameAndOptionalAlias) {
  auto ref = MakeBaseTableRef("users", "u");
  const auto& node = std::get<BaseTableRefNode>(ref->node);
  EXPECT_EQ(node.table_name, "users");
  ASSERT_TRUE(node.alias.has_value());
  EXPECT_EQ(*node.alias, "u");
}

TEST(AstTest, MakeBaseTableRefWithoutAlias) {
  auto ref = MakeBaseTableRef("users", std::nullopt);
  const auto& node = std::get<BaseTableRefNode>(ref->node);
  EXPECT_FALSE(node.alias.has_value());
}

TEST(AstTest, MakeJoinRefStoresBothSidesAndCondition) {
  auto left = MakeBaseTableRef("users", "u");
  auto right = MakeBaseTableRef("orders", "o");
  auto condition =
      MakeBinaryOp(BinaryOperator::kEqual, MakeColumnRef("u", "id"), MakeColumnRef("o", "user_id"));

  auto join = MakeJoinRef(std::move(left), std::move(right), std::move(condition));
  const auto& node = std::get<JoinRefNode>(join->node);

  EXPECT_EQ(std::get<BaseTableRefNode>(node.left->node).table_name, "users");
  EXPECT_EQ(std::get<BaseTableRefNode>(node.right->node).table_name, "orders");
  ASSERT_NE(node.on_condition, nullptr);
  EXPECT_EQ(std::get<BinaryOpNode>(node.on_condition->node).op, BinaryOperator::kEqual);
}

TEST(AstTest, MakeJoinRefAllowsNullConditionForCommaStyleJoin) {
  auto join = MakeJoinRef(MakeBaseTableRef("a", std::nullopt), MakeBaseTableRef("b", std::nullopt),
                          nullptr);
  const auto& node = std::get<JoinRefNode>(join->node);
  EXPECT_EQ(node.on_condition, nullptr);
}

}  // namespace
}  // namespace gistdb::binder