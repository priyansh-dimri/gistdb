#include "gistdb/optimizer/column_pruning.hpp"

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

using gistdb::binder::AggregateCall;
using gistdb::binder::AggregateFunctionKind;
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
using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
using gistdb::execution::ExpressionType;
using gistdb::execution::MakeArithmeticOp;
using gistdb::execution::MakeBooleanOp;
using gistdb::execution::MakeColumnRef;
using gistdb::execution::MakeIntConst;

std::unique_ptr<BoundExpression> ColRef(std::uint32_t table_id, std::uint32_t ordinal) {
  return MakeColumnRef(table_id, ordinal, gistdb::TypeId::kInteger);
}

TEST(ColumnPruningTest, ProjectionOnlyRequiresReferencedColumn) {
  auto scan = MakeLogicalScan(0, 100,
                              {{"a", ExpressionType::kInteger},
                               {"b", ExpressionType::kInteger},
                               {"c", ExpressionType::kInteger}});
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(scan), std::move(select_exprs),
                                    {{"a", ExpressionType::kInteger}});

  PruneColumns(*root);

  const auto& projection = std::get<LogicalProjection>(root->node);
  const auto& result_scan = std::get<LogicalScan>(projection.input->node);
  ASSERT_EQ(result_scan.required_ordinals.size(), 1U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
}

TEST(ColumnPruningTest, FilterColumnsAreRequiredEvenIfNotProjected) {
  auto scan = MakeLogicalScan(0, 100,
                              {{"a", ExpressionType::kInteger},
                               {"b", ExpressionType::kInteger},
                               {"c", ExpressionType::kInteger}});
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(5));
  auto filter = MakeLogicalFilter(std::move(scan), std::move(predicate));

  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(filter), std::move(select_exprs),
                                    {{"a", ExpressionType::kInteger}});

  PruneColumns(*root);

  const auto& projection = std::get<LogicalProjection>(root->node);
  const auto& result_filter = std::get<LogicalFilter>(projection.input->node);
  const auto& result_scan = std::get<LogicalScan>(result_filter.input->node);

  ASSERT_EQ(result_scan.required_ordinals.size(), 2U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
  EXPECT_EQ(result_scan.required_ordinals[1], 1U);
}

TEST(ColumnPruningTest, DuplicateReferencesAreDeduplicated) {
  auto scan = MakeLogicalScan(0, 100, {{"a", ExpressionType::kInteger}});
  auto lower = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1));
  auto upper = MakeBooleanOp(BinaryOperator::kLessThan, ColRef(0, 0), MakeIntConst(10));
  auto predicate = MakeBooleanOp(BinaryOperator::kAnd, std::move(lower), std::move(upper));
  auto filter = MakeLogicalFilter(std::move(scan), std::move(predicate));

  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(filter), std::move(select_exprs),
                                    {{"a", ExpressionType::kInteger}});

  PruneColumns(*root);

  const auto& projection = std::get<LogicalProjection>(root->node);
  const auto& result_filter = std::get<LogicalFilter>(projection.input->node);
  const auto& result_scan = std::get<LogicalScan>(result_filter.input->node);

  ASSERT_EQ(result_scan.required_ordinals.size(), 1U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
}

TEST(ColumnPruningTest, JoinEquiConditionColumnsAreAlwaysRequiredRegardlessOfSelectList) {
  auto left_scan = MakeLogicalScan(
      0, 100, {{"id", ExpressionType::kInteger}, {"key", ExpressionType::kInteger}});
  auto right_scan = MakeLogicalScan(
      1, 200, {{"key", ExpressionType::kInteger}, {"value", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 1, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(MakeIntConst(1));
  auto root = MakeLogicalProjection(std::move(join), std::move(select_exprs),
                                    {{"one", ExpressionType::kInteger}});

  PruneColumns(*root);

  const auto& projection = std::get<LogicalProjection>(root->node);
  const auto& result_join = std::get<LogicalJoin>(projection.input->node);
  const auto& left = std::get<LogicalScan>(result_join.left->node);
  const auto& right = std::get<LogicalScan>(result_join.right->node);

  ASSERT_EQ(left.required_ordinals.size(), 1U);
  EXPECT_EQ(left.required_ordinals[0], 1U);
  ASSERT_EQ(right.required_ordinals.size(), 1U);
  EXPECT_EQ(right.required_ordinals[0], 0U);
}

TEST(ColumnPruningTest, AggregateGroupByAndArgumentColumnsAreAlwaysRequired) {
  auto scan = MakeLogicalScan(0, 100,
                              {{"category", ExpressionType::kVarchar},
                               {"unused", ExpressionType::kInteger},
                               {"price", ExpressionType::kInteger}});
  std::vector<BoundColumnRef> group_by = {
      BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kVarchar}};
  std::vector<AggregateCall> aggregates = {AggregateCall{
      .function = AggregateFunctionKind::kSum,
      .argument = BoundColumnRef{.table_id = 0, .ordinal = 2, .type = gistdb::TypeId::kInteger}}};
  auto root = MakeLogicalAggregate(
      std::move(scan), group_by, aggregates,
      {{"category", ExpressionType::kVarchar}, {"sum_price", ExpressionType::kInteger}});

  PruneColumns(*root);

  const auto& aggregate = std::get<LogicalAggregate>(root->node);
  const auto& result_scan = std::get<LogicalScan>(aggregate.input->node);

  ASSERT_EQ(result_scan.required_ordinals.size(), 2U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
  EXPECT_EQ(result_scan.required_ordinals[1], 2U);
}

TEST(ColumnPruningTest, AggregateDoesNotInheritRequirementsFromAbove) {
  auto scan = MakeLogicalScan(
      0, 100, {{"category", ExpressionType::kVarchar}, {"other", ExpressionType::kInteger}});
  std::vector<BoundColumnRef> group_by = {
      BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kVarchar}};
  auto aggregate =
      MakeLogicalAggregate(std::move(scan), group_by, {}, {{"category", ExpressionType::kVarchar}});

  auto synthetic_predicate =
      MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 1), MakeIntConst(5));
  auto root = MakeLogicalFilter(std::move(aggregate), std::move(synthetic_predicate));

  PruneColumns(*root);

  const auto& filter = std::get<LogicalFilter>(root->node);
  const auto& result_aggregate = std::get<LogicalAggregate>(filter.input->node);
  const auto& result_scan = std::get<LogicalScan>(result_aggregate.input->node);
  ASSERT_EQ(result_scan.required_ordinals.size(), 1U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
}

TEST(ColumnPruningTest, NestedJoinPropagatesBindingsToTheCorrectSide) {
  auto scan_a = MakeLogicalScan(0, 100, {{"a_key", ExpressionType::kInteger}});
  auto scan_b = MakeLogicalScan(
      1, 200, {{"b_key", ExpressionType::kInteger}, {"b_val", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> inner_equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto inner_join = MakeLogicalJoin(std::move(scan_a), std::move(scan_b), std::move(inner_equi));

  auto scan_c = MakeLogicalScan(2, 300, {{"c_key", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> outer_equi = {
      {BoundColumnRef{.table_id = 1, .ordinal = 1, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 2, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto outer_join =
      MakeLogicalJoin(std::move(inner_join), std::move(scan_c), std::move(outer_equi));

  PruneColumns(*outer_join);

  const auto& result_outer = std::get<LogicalJoin>(outer_join->node);
  const auto& result_inner = std::get<LogicalJoin>(result_outer.left->node);
  const auto& result_scan_a = std::get<LogicalScan>(result_inner.left->node);
  const auto& result_scan_b = std::get<LogicalScan>(result_inner.right->node);
  const auto& result_scan_c = std::get<LogicalScan>(result_outer.right->node);

  ASSERT_EQ(result_scan_a.required_ordinals.size(), 1U);
  EXPECT_EQ(result_scan_a.required_ordinals[0], 0U);

  ASSERT_EQ(result_scan_b.required_ordinals.size(), 2U);
  EXPECT_EQ(result_scan_b.required_ordinals[0], 0U);
  EXPECT_EQ(result_scan_b.required_ordinals[1], 1U);

  ASSERT_EQ(result_scan_c.required_ordinals.size(), 1U);
  EXPECT_EQ(result_scan_c.required_ordinals[0], 0U);
}

TEST(ColumnPruningTest, RequiredOrdinalsEndUpSortedRegardlessOfReferenceOrder) {
  auto scan = MakeLogicalScan(0, 100,
                              {{"a", ExpressionType::kInteger},
                               {"b", ExpressionType::kInteger},
                               {"c", ExpressionType::kInteger}});
  auto high = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 2), MakeIntConst(1));
  auto low = MakeBooleanOp(BinaryOperator::kEqual, ColRef(0, 0), MakeIntConst(2));
  auto predicate = MakeBooleanOp(BinaryOperator::kAnd, std::move(high), std::move(low));
  auto root = MakeLogicalFilter(std::move(scan), std::move(predicate));

  PruneColumns(*root);

  const auto& filter = std::get<LogicalFilter>(root->node);
  const auto& result_scan = std::get<LogicalScan>(filter.input->node);

  ASSERT_EQ(result_scan.required_ordinals.size(), 2U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
  EXPECT_EQ(result_scan.required_ordinals[1], 2U);
}

TEST(ColumnPruningTest, BareScanWithNoParentEndsUpWithNoRequiredColumns) {
  auto root =
      MakeLogicalScan(0, 100, {{"a", ExpressionType::kInteger}, {"b", ExpressionType::kInteger}});
  PruneColumns(*root);

  const auto& result_scan = std::get<LogicalScan>(root->node);
  EXPECT_TRUE(result_scan.required_ordinals.empty());
}

TEST(ColumnPruningTest, ProjectionExpressionColumnsAreAllRequired) {
  auto scan = MakeLogicalScan(0, 100,
                              {{"price", ExpressionType::kInteger},
                               {"quantity", ExpressionType::kInteger},
                               {"unused", ExpressionType::kInteger}});
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(MakeArithmeticOp(BinaryOperator::kMultiply, ColRef(0, 0), ColRef(0, 1),
                                          ExpressionType::kInteger));
  auto root = MakeLogicalProjection(std::move(scan), std::move(select_exprs),
                                    {{"total", ExpressionType::kInteger}});

  PruneColumns(*root);

  const auto& projection = std::get<LogicalProjection>(root->node);
  const auto& result_scan = std::get<LogicalScan>(projection.input->node);

  ASSERT_EQ(result_scan.required_ordinals.size(), 2U);
  EXPECT_EQ(result_scan.required_ordinals[0], 0U);
  EXPECT_EQ(result_scan.required_ordinals[1], 1U);
}

}  // namespace
}  // namespace gistdb::optimizer