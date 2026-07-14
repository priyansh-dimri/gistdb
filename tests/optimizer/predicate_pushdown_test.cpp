#include "gistdb/optimizer/predicate_pushdown.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/types.hpp"

namespace gistdb::optimizer {
namespace {

using gistdb::binder::LogicalAggregate;
using gistdb::binder::LogicalFilter;
using gistdb::binder::LogicalJoin;
using gistdb::binder::LogicalProjection;
using gistdb::binder::LogicalScan;
using gistdb::binder::MakeLogicalAggregate;
using gistdb::binder::MakeLogicalFilter;
using gistdb::binder::MakeLogicalJoin;
using gistdb::binder::MakeLogicalProjection;
using gistdb::binder::MakeLogicalScan;
using gistdb::execution::BinaryOperator;
using gistdb::execution::BinaryOpNode;
using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
using gistdb::execution::MakeBooleanOp;
using gistdb::execution::MakeColumnRef;
using gistdb::execution::MakeIntConst;

std::unique_ptr<BoundExpression> ColRef(std::uint32_t table_id, std::uint32_t ordinal) {
  return MakeColumnRef(table_id, ordinal, gistdb::TypeId::kInteger);
}

TEST(PredicatePushdownTest, NoOpWhenThereIsNoFilterAtAll) {
  auto scan = MakeLogicalScan(0, 100, {});
  auto projection = MakeLogicalProjection(std::move(scan), {}, {});

  auto result = PushdownPredicates(std::move(projection));

  const auto& proj = std::get<LogicalProjection>(result->node);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(proj.input->node));
}

TEST(PredicatePushdownTest, SingleTableFilterEndsUpDirectlyAboveItsScan) {
  auto scan = MakeLogicalScan(0, 100, {});
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(5));
  const BoundExpression* raw_pred = predicate.get();
  auto filter = MakeLogicalFilter(std::move(scan), std::move(predicate));

  auto result = PushdownPredicates(std::move(filter));

  const auto& result_filter = std::get<LogicalFilter>(result->node);
  EXPECT_EQ(result_filter.predicate.get(), raw_pred);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_filter.input->node));
}

TEST(PredicatePushdownTest, MultipleConjunctsOnASingleScanAreFusedIntoOneFilter) {
  auto scan = MakeLogicalScan(0, 100, {});
  auto a = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  auto b = MakeBooleanOp(BinaryOperator::kLessThan, ColRef(0, 1), MakeIntConst(10));
  auto ab = MakeBooleanOp(BinaryOperator::kAnd, std::move(a), std::move(b));
  auto filter = MakeLogicalFilter(std::move(scan), std::move(ab));

  auto result = PushdownPredicates(std::move(filter));

  const auto& result_filter = std::get<LogicalFilter>(result->node);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_filter.input->node));
  EXPECT_TRUE(std::holds_alternative<BinaryOpNode>(result_filter.predicate->node));
}

TEST(PredicatePushdownTest, ConjunctReferencingOnlyLeftSidePushesBelowJoin) {
  auto left_scan = MakeLogicalScan(0, 100, {});
  auto right_scan = MakeLogicalScan(1, 200, {});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(5));
  const BoundExpression* raw_pred = predicate.get();
  auto filter = MakeLogicalFilter(std::move(join), std::move(predicate));

  auto result = PushdownPredicates(std::move(filter));

  ASSERT_TRUE(std::holds_alternative<LogicalJoin>(result->node));
  const auto& result_join = std::get<LogicalJoin>(result->node);

  const auto& left_filter = std::get<LogicalFilter>(result_join.left->node);
  EXPECT_EQ(left_filter.predicate.get(), raw_pred);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(left_filter.input->node));
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_join.right->node));
}

TEST(PredicatePushdownTest, ConjunctReferencingBothSidesStaysAboveJoin) {
  auto left_scan = MakeLogicalScan(0, 100, {});
  auto right_scan = MakeLogicalScan(1, 200, {});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), ColRef(1, 1));
  const BoundExpression* raw_pred = predicate.get();
  auto filter = MakeLogicalFilter(std::move(join), std::move(predicate));

  auto result = PushdownPredicates(std::move(filter));

  const auto& result_filter = std::get<LogicalFilter>(result->node);
  EXPECT_EQ(result_filter.predicate.get(), raw_pred);

  const auto& result_join = std::get<LogicalJoin>(result_filter.input->node);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_join.left->node));
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_join.right->node));
}

TEST(PredicatePushdownTest, ConjunctsSplitAcrossBothSidesOfAJoinPlusOneThatStays) {
  auto left_scan = MakeLogicalScan(0, 100, {});
  auto right_scan = MakeLogicalScan(1, 200, {});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  auto left_only = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(5));
  auto right_only = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(1, 1), MakeIntConst(10));
  auto both_sides = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 2), ColRef(1, 2));

  const BoundExpression* raw_left_only = left_only.get();
  const BoundExpression* raw_right_only = right_only.get();
  const BoundExpression* raw_both_sides = both_sides.get();

  auto combined = MakeBooleanOp(
      BinaryOperator::kAnd,
      MakeBooleanOp(BinaryOperator::kAnd, std::move(left_only), std::move(right_only)),
      std::move(both_sides));
  auto filter = MakeLogicalFilter(std::move(join), std::move(combined));

  auto result = PushdownPredicates(std::move(filter));
  const auto& result_filter = std::get<LogicalFilter>(result->node);
  EXPECT_EQ(result_filter.predicate.get(), raw_both_sides);

  const auto& result_join = std::get<LogicalJoin>(result_filter.input->node);

  const auto& left_filter = std::get<LogicalFilter>(result_join.left->node);
  EXPECT_EQ(left_filter.predicate.get(), raw_left_only);

  const auto& right_filter = std::get<LogicalFilter>(result_join.right->node);
  EXPECT_EQ(right_filter.predicate.get(), raw_right_only);
}

TEST(PredicatePushdownTest, JoinEquiConditionsAreUntouchedByPushdown) {
  auto left_scan = MakeLogicalScan(0, 100, {});
  auto right_scan = MakeLogicalScan(1, 200, {});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 3, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 4, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), equi);

  auto result = PushdownPredicates(std::move(join));

  const auto& result_join = std::get<LogicalJoin>(result->node);
  ASSERT_EQ(result_join.equi_conditions.size(), 1U);
  EXPECT_EQ(result_join.equi_conditions[0].first.ordinal, 3U);
  EXPECT_EQ(result_join.equi_conditions[0].second.ordinal, 4U);
}

TEST(PredicatePushdownTest, ConjunctReferencingOnlyGroupByColumnCrossesBelowAggregate) {
  auto scan = MakeLogicalScan(0, 100, {});
  std::vector<BoundColumnRef> group_by = {
      BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kVarchar}};
  auto aggregate = MakeLogicalAggregate(std::move(scan), group_by, {}, {});

  auto predicate = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 0), MakeIntConst(5));
  const BoundExpression* raw_pred = predicate.get();
  auto filter = MakeLogicalFilter(std::move(aggregate), std::move(predicate));

  auto result = PushdownPredicates(std::move(filter));
  ASSERT_TRUE(std::holds_alternative<LogicalAggregate>(result->node));
  const auto& result_agg = std::get<LogicalAggregate>(result->node);

  const auto& below_filter = std::get<LogicalFilter>(result_agg.input->node);
  EXPECT_EQ(below_filter.predicate.get(), raw_pred);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(below_filter.input->node));
}

TEST(PredicatePushdownTest, ConjunctReferencingNonGroupByColumnStaysAboveAggregate) {
  auto scan = MakeLogicalScan(0, 100, {});
  std::vector<BoundColumnRef> group_by = {
      BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kVarchar}};
  auto aggregate = MakeLogicalAggregate(std::move(scan), group_by, {}, {});
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(100));
  const BoundExpression* raw_pred = predicate.get();
  auto filter = MakeLogicalFilter(std::move(aggregate), std::move(predicate));

  auto result = PushdownPredicates(std::move(filter));

  const auto& result_filter = std::get<LogicalFilter>(result->node);
  EXPECT_EQ(result_filter.predicate.get(), raw_pred);

  const auto& result_agg = std::get<LogicalAggregate>(result_filter.input->node);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_agg.input->node));
}

TEST(PredicatePushdownTest, ProjectionIsTransparentToPushdown) {
  auto scan = MakeLogicalScan(0, 100, {});
  auto projection = MakeLogicalProjection(std::move(scan), {}, {});

  auto right_scan = MakeLogicalScan(1, 200, {});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(projection), std::move(right_scan), std::move(equi));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(5));
  const BoundExpression* raw_pred = predicate.get();
  auto filter = MakeLogicalFilter(std::move(join), std::move(predicate));

  auto result = PushdownPredicates(std::move(filter));

  ASSERT_TRUE(std::holds_alternative<LogicalJoin>(result->node));
  const auto& result_join = std::get<LogicalJoin>(result->node);

  const auto& projection_node = std::get<LogicalProjection>(result_join.left->node);
  const auto& below_filter = std::get<LogicalFilter>(projection_node.input->node);
  EXPECT_EQ(below_filter.predicate.get(), raw_pred);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(below_filter.input->node));
}

TEST(PredicatePushdownTest, TwoDirectlyStackedFiltersAreFusedIntoOne) {
  auto scan = MakeLogicalScan(0, 100, {});
  auto inner_pred = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  auto outer_pred = MakeBooleanOp(BinaryOperator::kLessThan, ColRef(0, 1), MakeIntConst(10));
  const BoundExpression* raw_inner = inner_pred.get();
  const BoundExpression* raw_outer = outer_pred.get();

  auto inner_filter = MakeLogicalFilter(std::move(scan), std::move(inner_pred));
  auto outer_filter = MakeLogicalFilter(std::move(inner_filter), std::move(outer_pred));

  auto result = PushdownPredicates(std::move(outer_filter));

  const auto& result_filter = std::get<LogicalFilter>(result->node);
  EXPECT_TRUE(std::holds_alternative<LogicalScan>(result_filter.input->node));

  const auto& combined = std::get<BinaryOpNode>(result_filter.predicate->node);
  EXPECT_EQ(combined.op, BinaryOperator::kAnd);
  EXPECT_EQ(combined.left.get(), raw_inner);
  EXPECT_EQ(combined.right.get(), raw_outer);
}

}  // namespace
}  // namespace gistdb::optimizer