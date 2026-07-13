#include "gistdb/binder/logical_plan.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/types.hpp"

namespace gistdb::binder {
namespace {

using gistdb::execution::BoundColumnRef;
using gistdb::execution::ExpressionType;
using gistdb::execution::MakeIntConst;

TEST(LogicalPlanTest, ScanReturnsItsOwnOutputColumns) {
  std::vector<OutputColumn> columns = {{"id", ExpressionType::kInteger},
                                       {"name", ExpressionType::kVarchar}};
  auto scan = MakeLogicalScan(7, columns);

  EXPECT_EQ(std::get<LogicalScan>(scan->node).table_id, 7U);
  std::vector<OutputColumn> schema = OutputSchema(*scan);
  ASSERT_EQ(schema.size(), 2U);
  EXPECT_EQ(schema[0].display_name, "id");
  EXPECT_EQ(schema[1].display_name, "name");
}

TEST(LogicalPlanTest, FilterHasNoOwnSchemaAndRecursesIntoInput) {
  std::vector<OutputColumn> columns = {{"id", ExpressionType::kInteger}};
  auto scan = MakeLogicalScan(1, columns);
  auto filter = MakeLogicalFilter(std::move(scan), MakeIntConst(1));

  std::vector<OutputColumn> schema = OutputSchema(*filter);
  ASSERT_EQ(schema.size(), 1U);
  EXPECT_EQ(schema[0].display_name, "id");
}

TEST(LogicalPlanTest, JoinConcatenatesLeftThenRightSchema) {
  auto left = MakeLogicalScan(1, {{"id", ExpressionType::kInteger}});
  auto right = MakeLogicalScan(2, {{"user_id", ExpressionType::kInteger}});

  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi_conditions;
  equi_conditions.emplace_back(
      BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger},
      BoundColumnRef{.table_id = 2, .ordinal = 0, .type = gistdb::TypeId::kInteger});

  auto join = MakeLogicalJoin(std::move(left), std::move(right), std::move(equi_conditions));

  std::vector<OutputColumn> schema = OutputSchema(*join);
  ASSERT_EQ(schema.size(), 2U);
  EXPECT_EQ(schema[0].display_name, "id");
  EXPECT_EQ(schema[1].display_name, "user_id");
}

TEST(LogicalPlanTest, JoinStoresEquiConditionsAsColumnRefPairs) {
  auto left = MakeLogicalScan(1, {{"id", ExpressionType::kInteger}});
  auto right = MakeLogicalScan(2, {{"user_id", ExpressionType::kInteger}});

  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi_conditions;
  equi_conditions.emplace_back(
      BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger},
      BoundColumnRef{.table_id = 2, .ordinal = 0, .type = gistdb::TypeId::kInteger});

  auto join = MakeLogicalJoin(std::move(left), std::move(right), std::move(equi_conditions));

  const auto& stored = std::get<LogicalJoin>(join->node).equi_conditions;
  ASSERT_EQ(stored.size(), 1U);
  EXPECT_EQ(stored[0].first.table_id, 1U);
  EXPECT_EQ(stored[0].second.table_id, 2U);
}

TEST(LogicalPlanTest, AggregateReturnsItsOwnOutputColumnsNotInputs) {
  auto scan = MakeLogicalScan(
      1, {{"category", ExpressionType::kVarchar}, {"price", ExpressionType::kInteger}});

  std::vector<BoundColumnRef> group_by = {
      BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kVarchar}};
  std::vector<AggregateCall> aggregates = {AggregateCall{
      .function = AggregateFunctionKind::kSum,
      .argument = BoundColumnRef{.table_id = 1, .ordinal = 1, .type = gistdb::TypeId::kInteger}}};
  std::vector<OutputColumn> output_columns = {{"category", ExpressionType::kVarchar},
                                              {"sum_price", ExpressionType::kInteger}};

  auto aggregate = MakeLogicalAggregate(std::move(scan), std::move(group_by), std::move(aggregates),
                                        output_columns);

  std::vector<OutputColumn> schema = OutputSchema(*aggregate);
  ASSERT_EQ(schema.size(), 2U);
  EXPECT_EQ(schema[0].display_name, "category");
  EXPECT_EQ(schema[1].display_name, "sum_price");
}

TEST(LogicalPlanTest, CountStarAggregateCallHasNoArgument) {
  AggregateCall call{.function = AggregateFunctionKind::kCountStar, .argument = std::nullopt};
  EXPECT_FALSE(call.argument.has_value());
}

TEST(LogicalPlanTest, SumAggregateCallCarriesItsArgumentColumn) {
  AggregateCall call{
      .function = AggregateFunctionKind::kSum,
      .argument = BoundColumnRef{.table_id = 1, .ordinal = 1, .type = gistdb::TypeId::kInteger}};
  ASSERT_TRUE(call.argument.has_value());
  EXPECT_EQ(call.argument->ordinal, 1U);
}

TEST(LogicalPlanTest, ProjectionReturnsItsOwnOutputColumns) {
  auto scan = MakeLogicalScan(1, {{"price", ExpressionType::kInteger}});

  std::vector<std::unique_ptr<gistdb::execution::BoundExpression>> select_expressions;
  select_expressions.push_back(MakeIntConst(1));
  std::vector<OutputColumn> output_columns = {{"one", ExpressionType::kInteger}};

  auto projection =
      MakeLogicalProjection(std::move(scan), std::move(select_expressions), output_columns);

  std::vector<OutputColumn> schema = OutputSchema(*projection);
  ASSERT_EQ(schema.size(), 1U);
  EXPECT_EQ(schema[0].display_name, "one");
}

}  // namespace
}  // namespace gistdb::binder