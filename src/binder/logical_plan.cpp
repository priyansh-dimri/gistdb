#include "gistdb/binder/logical_plan.hpp"

#include <iterator>
#include <type_traits>
#include <utility>

namespace gistdb::binder {

std::vector<OutputColumn> OutputSchema(const LogicalPlanNode& node) {
  return std::visit(
      [](const auto& n) -> std::vector<OutputColumn> {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, LogicalScan> || std::is_same_v<T, LogicalAggregate> ||
                      std::is_same_v<T, LogicalProjection>) {
          return n.output_columns;
        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          return OutputSchema(*n.input);
        } else {
          auto left_cols = OutputSchema(*n.left);
          auto right_cols = OutputSchema(*n.right);
          left_cols.insert(left_cols.end(), std::make_move_iterator(right_cols.begin()),
                           std::make_move_iterator(right_cols.end()));
          return left_cols;
        }
      },
      node.node);
}

std::unique_ptr<LogicalPlanNode> MakeLogicalScan(std::uint32_t table_id,
                                                 std::vector<OutputColumn> output_columns) {
  auto plan = std::make_unique<LogicalPlanNode>();
  plan->node = LogicalScan{.table_id = table_id, .output_columns = std::move(output_columns)};
  return plan;
}

std::unique_ptr<LogicalPlanNode> MakeLogicalFilter(
    std::unique_ptr<LogicalPlanNode> input,
    std::unique_ptr<gistdb::execution::BoundExpression> predicate) {
  auto plan = std::make_unique<LogicalPlanNode>();
  plan->node = LogicalFilter{.input = std::move(input), .predicate = std::move(predicate)};
  return plan;
}

std::unique_ptr<LogicalPlanNode> MakeLogicalJoin(
    std::unique_ptr<LogicalPlanNode> left, std::unique_ptr<LogicalPlanNode> right,
    std::vector<std::pair<gistdb::execution::BoundColumnRef, gistdb::execution::BoundColumnRef>>
        equi_conditions) {
  auto plan = std::make_unique<LogicalPlanNode>();
  plan->node = LogicalJoin{.left = std::move(left),
                           .right = std::move(right),
                           .equi_conditions = std::move(equi_conditions)};
  return plan;
}

std::unique_ptr<LogicalPlanNode> MakeLogicalAggregate(
    std::unique_ptr<LogicalPlanNode> input, std::vector<gistdb::execution::BoundColumnRef> group_by,
    std::vector<AggregateCall> aggregates, std::vector<OutputColumn> output_columns) {
  auto plan = std::make_unique<LogicalPlanNode>();
  plan->node = LogicalAggregate{.input = std::move(input),
                                .group_by = std::move(group_by),
                                .aggregates = std::move(aggregates),
                                .output_columns = std::move(output_columns)};
  return plan;
}

std::unique_ptr<LogicalPlanNode> MakeLogicalProjection(
    std::unique_ptr<LogicalPlanNode> input,
    std::vector<std::unique_ptr<gistdb::execution::BoundExpression>> select_expressions,
    std::vector<OutputColumn> output_columns) {
  auto plan = std::make_unique<LogicalPlanNode>();
  plan->node = LogicalProjection{.input = std::move(input),
                                 .select_expressions = std::move(select_expressions),
                                 .output_columns = std::move(output_columns)};
  return plan;
}

}  // namespace gistdb::binder