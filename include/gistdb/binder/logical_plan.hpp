#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::binder {

struct LogicalPlanNode;

struct OutputColumn {
  std::string display_name;
  gistdb::execution::ExpressionType type;
};

struct LogicalScan {
  std::uint32_t binding_id;
  std::uint32_t physical_table_id;
  std::vector<OutputColumn> output_columns;
};

struct LogicalFilter {
  std::unique_ptr<LogicalPlanNode> input;
  std::unique_ptr<gistdb::execution::BoundExpression> predicate;
};

struct LogicalJoin {
  std::unique_ptr<LogicalPlanNode> left;
  std::unique_ptr<LogicalPlanNode> right;
  std::vector<std::pair<gistdb::execution::BoundColumnRef, gistdb::execution::BoundColumnRef>>
      equi_conditions;
};

enum class AggregateFunctionKind : std::uint8_t {
  kCountStar,
  kCount,
  kSum,
  kAvg,
  kMin,
  kMax,
};

struct AggregateCall {
  AggregateFunctionKind function = AggregateFunctionKind::kCountStar;
  std::optional<gistdb::execution::BoundColumnRef> argument;
};

struct LogicalAggregate {
  std::unique_ptr<LogicalPlanNode> input;
  std::vector<gistdb::execution::BoundColumnRef> group_by;
  std::vector<AggregateCall> aggregates;
  std::vector<OutputColumn> output_columns;
};

struct LogicalProjection {
  std::unique_ptr<LogicalPlanNode> input;
  std::vector<std::unique_ptr<gistdb::execution::BoundExpression>> select_expressions;
  std::vector<OutputColumn> output_columns;
};

struct LogicalPlanNode {
  std::variant<LogicalScan, LogicalFilter, LogicalJoin, LogicalAggregate, LogicalProjection> node;
};

[[nodiscard]] std::vector<OutputColumn> OutputSchema(const LogicalPlanNode& node);
[[nodiscard]] std::vector<std::uint32_t> CollectBindingIds(const LogicalPlanNode& node);

[[nodiscard]] std::unique_ptr<LogicalPlanNode> MakeLogicalScan(
    std::uint32_t binding_id, std::uint32_t physical_table_id,
    std::vector<OutputColumn> output_columns);
[[nodiscard]] std::unique_ptr<LogicalPlanNode> MakeLogicalFilter(
    std::unique_ptr<LogicalPlanNode> input,
    std::unique_ptr<gistdb::execution::BoundExpression> predicate);
[[nodiscard]] std::unique_ptr<LogicalPlanNode> MakeLogicalJoin(
    std::unique_ptr<LogicalPlanNode> left, std::unique_ptr<LogicalPlanNode> right,
    std::vector<std::pair<gistdb::execution::BoundColumnRef, gistdb::execution::BoundColumnRef>>
        equi_conditions);
[[nodiscard]] std::unique_ptr<LogicalPlanNode> MakeLogicalAggregate(
    std::unique_ptr<LogicalPlanNode> input, std::vector<gistdb::execution::BoundColumnRef> group_by,
    std::vector<AggregateCall> aggregates, std::vector<OutputColumn> output_columns);
[[nodiscard]] std::unique_ptr<LogicalPlanNode> MakeLogicalProjection(
    std::unique_ptr<LogicalPlanNode> input,
    std::vector<std::unique_ptr<gistdb::execution::BoundExpression>> select_expressions,
    std::vector<OutputColumn> output_columns);

}  // namespace gistdb::binder