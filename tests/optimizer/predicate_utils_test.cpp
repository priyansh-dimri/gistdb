#include "gistdb/optimizer/predicate_utils.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/types.hpp"

namespace gistdb::optimizer {
namespace {

using gistdb::execution::BinaryOperator;
using gistdb::execution::BinaryOpNode;
using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
using gistdb::execution::ExpressionType;
using gistdb::execution::MakeArithmeticOp;
using gistdb::execution::MakeBooleanOp;
using gistdb::execution::MakeColumnRef;
using gistdb::execution::MakeIntConst;
using gistdb::execution::MakeNot;

std::unique_ptr<BoundExpression> ColRef(std::uint32_t table_id, std::uint32_t ordinal) {
  return MakeColumnRef(table_id, ordinal, gistdb::TypeId::kInteger);
}

TEST(PredicateUtilsTest, FlattenConjunctsSplitsTopLevelAnds) {
  auto a = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  auto b = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(2));
  auto c = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 2), MakeIntConst(3));
  const BoundExpression* raw_a = a.get();
  const BoundExpression* raw_b = b.get();
  const BoundExpression* raw_c = c.get();

  auto ab = MakeBooleanOp(BinaryOperator::kAnd, std::move(a), std::move(b));
  auto abc = MakeBooleanOp(BinaryOperator::kAnd, std::move(ab), std::move(c));

  std::vector<const BoundExpression*> conjuncts = FlattenConjuncts(*abc);
  ASSERT_EQ(conjuncts.size(), 3U);
  EXPECT_EQ(conjuncts[0], raw_a);
  EXPECT_EQ(conjuncts[1], raw_b);
  EXPECT_EQ(conjuncts[2], raw_c);
}

TEST(PredicateUtilsTest, FlattenConjunctsKeepsOrWhole) {
  auto x = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 0), MakeIntConst(1));
  auto y = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 1), MakeIntConst(2));
  auto z = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 2), MakeIntConst(3));

  auto xy = MakeBooleanOp(BinaryOperator::kOr, std::move(x), std::move(y));
  const BoundExpression* raw_xy = xy.get();
  const BoundExpression* raw_z = z.get();
  auto whole = MakeBooleanOp(BinaryOperator::kAnd, std::move(xy), std::move(z));

  std::vector<const BoundExpression*> conjuncts = FlattenConjuncts(*whole);
  ASSERT_EQ(conjuncts.size(), 2U);
  EXPECT_EQ(conjuncts[0], raw_xy);
  EXPECT_EQ(conjuncts[1], raw_z);
}

TEST(PredicateUtilsTest, FlattenConjunctsHandlesSingleNonAndExpression) {
  auto expr = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(5));
  const BoundExpression* raw = expr.get();

  std::vector<const BoundExpression*> conjuncts = FlattenConjuncts(*expr);
  ASSERT_EQ(conjuncts.size(), 1U);
  EXPECT_EQ(conjuncts[0], raw);
}

TEST(PredicateUtilsTest, ExtractConjunctsMovesEachConjunctOutOwning) {
  auto a = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  auto b = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(2));
  const BoundExpression* raw_a = a.get();
  const BoundExpression* raw_b = b.get();

  auto ab = MakeBooleanOp(BinaryOperator::kAnd, std::move(a), std::move(b));
  std::vector<std::unique_ptr<BoundExpression>> extracted = ExtractConjuncts(std::move(ab));

  ASSERT_EQ(extracted.size(), 2U);
  EXPECT_EQ(extracted[0].get(), raw_a);
  EXPECT_EQ(extracted[1].get(), raw_b);
}

TEST(PredicateUtilsTest, ExtractConjunctsKeepsOrWholeAsOwningUnit) {
  auto x = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 0), MakeIntConst(1));
  auto y = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 1), MakeIntConst(2));
  auto xy = MakeBooleanOp(BinaryOperator::kOr, std::move(x), std::move(y));
  const BoundExpression* raw_xy = xy.get();

  std::vector<std::unique_ptr<BoundExpression>> extracted = ExtractConjuncts(std::move(xy));
  ASSERT_EQ(extracted.size(), 1U);
  EXPECT_EQ(extracted[0].get(), raw_xy);
}

TEST(PredicateUtilsTest, RebuildConjunctionOfSingleConjunctReturnsItUnchanged) {
  auto a = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  const BoundExpression* raw_a = a.get();

  std::vector<std::unique_ptr<BoundExpression>> conjuncts;
  conjuncts.push_back(std::move(a));
  std::unique_ptr<BoundExpression> rebuilt = RebuildConjunction(std::move(conjuncts));

  EXPECT_EQ(rebuilt.get(), raw_a);
}

TEST(PredicateUtilsTest, RebuildConjunctionAndsMultipleConjunctsLeftAssociatively) {
  auto a = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  auto b = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(2));
  auto c = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 2), MakeIntConst(3));
  const BoundExpression* raw_a = a.get();
  const BoundExpression* raw_b = b.get();
  const BoundExpression* raw_c = c.get();

  std::vector<std::unique_ptr<BoundExpression>> conjuncts;
  conjuncts.push_back(std::move(a));
  conjuncts.push_back(std::move(b));
  conjuncts.push_back(std::move(c));
  std::unique_ptr<BoundExpression> rebuilt = RebuildConjunction(std::move(conjuncts));

  EXPECT_EQ(rebuilt->ResultType(), ExpressionType::kBoolean);
  const auto& top = std::get<BinaryOpNode>(rebuilt->node);
  EXPECT_EQ(top.op, BinaryOperator::kAnd);
  EXPECT_EQ(top.right.get(), raw_c);

  const auto& inner = std::get<BinaryOpNode>(top.left->node);
  EXPECT_EQ(inner.op, BinaryOperator::kAnd);
  EXPECT_EQ(inner.left.get(), raw_a);
  EXPECT_EQ(inner.right.get(), raw_b);
}

TEST(PredicateUtilsTest, CollectColumnRefsFindsColumnsThroughArithmeticComparisonAndNot) {
  auto sum =
      MakeArithmeticOp(BinaryOperator::kAdd, ColRef(1, 0), ColRef(1, 1), ExpressionType::kInteger);
  auto gt = MakeBooleanOp(BinaryOperator::kGreaterThan, std::move(sum), MakeIntConst(5));
  auto eq = MakeBooleanOp(BinaryOperator::kEqual, ColRef(2, 0), MakeIntConst(3));
  auto not_equal = MakeNot(std::move(eq));
  auto expr = MakeBooleanOp(BinaryOperator::kAnd, std::move(gt), std::move(not_equal));

  std::vector<BoundColumnRef> refs = CollectColumnRefs(*expr);
  ASSERT_EQ(refs.size(), 3U);
  EXPECT_EQ(refs[0].table_id, 1U);
  EXPECT_EQ(refs[0].ordinal, 0U);
  EXPECT_EQ(refs[1].table_id, 1U);
  EXPECT_EQ(refs[1].ordinal, 1U);
  EXPECT_EQ(refs[2].table_id, 2U);
  EXPECT_EQ(refs[2].ordinal, 0U);
}

TEST(PredicateUtilsTest, CollectColumnRefsFindsNothingInAConstOnlyExpression) {
  auto expr = MakeBooleanOp(BinaryOperator::kEqual, MakeIntConst(1), MakeIntConst(1));
  std::vector<BoundColumnRef> refs = CollectColumnRefs(*expr);
  EXPECT_TRUE(refs.empty());
}

TEST(PredicateUtilsTest, AllColumnsAvailableTrueWhenEveryTableIdIsInTheSet) {
  auto expr = MakeBooleanOp(BinaryOperator::kEqual, ColRef(1, 0), ColRef(2, 0));
  EXPECT_TRUE(AllColumnsAvailable(*expr, {1, 2}));
  EXPECT_TRUE(AllColumnsAvailable(*expr, {1, 2, 3}));
}

TEST(PredicateUtilsTest, AllColumnsAvailableFalseWhenAnyTableIdIsMissing) {
  auto expr = MakeBooleanOp(BinaryOperator::kEqual, ColRef(1, 0), ColRef(2, 0));
  EXPECT_FALSE(AllColumnsAvailable(*expr, {1}));
  EXPECT_FALSE(AllColumnsAvailable(*expr, {2}));
  EXPECT_FALSE(AllColumnsAvailable(*expr, {}));
}

TEST(PredicateUtilsTest, AllColumnsAvailableTrueForConstOnlyExpression) {
  auto expr = MakeBooleanOp(BinaryOperator::kEqual, MakeIntConst(1), MakeIntConst(1));
  EXPECT_TRUE(AllColumnsAvailable(*expr, {}));
}

}  // namespace
}  // namespace gistdb::optimizer