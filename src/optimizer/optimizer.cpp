#include "gistdb/optimizer/optimizer.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "gistdb/execution/aggregation_operator.hpp"
#include "gistdb/execution/filter_operator.hpp"
#include "gistdb/execution/hash_join_operator.hpp"
#include "gistdb/execution/projection_operator.hpp"
#include "gistdb/execution/seq_scan_operator.hpp"
#include "gistdb/optimizer/column_pruning.hpp"
#include "gistdb/optimizer/predicate_pushdown.hpp"
#include "gistdb/types.hpp"

namespace gistdb::optimizer {

namespace {

using gistdb::binder::LogicalAggregate;
using gistdb::binder::LogicalFilter;
using gistdb::binder::LogicalJoin;
using gistdb::binder::LogicalPlanNode;
using gistdb::binder::LogicalProjection;
using gistdb::binder::LogicalScan;
using gistdb::execution::BoundColumnRef;

[[nodiscard]] gistdb::TypeId ToTypeId(gistdb::execution::ExpressionType type) {
  switch (type) {
    case gistdb::execution::ExpressionType::kInteger:
      return gistdb::TypeId::kInteger;
    case gistdb::execution::ExpressionType::kFloat:
      return gistdb::TypeId::kFloat;
    case gistdb::execution::ExpressionType::kVarchar:
      return gistdb::TypeId::kVarchar;
    case gistdb::execution::ExpressionType::kBoolean:
      throw std::runtime_error(
          "A stored/materialized column resolved to BOOLEAN -- BOOLEAN is an "
          "internal-only predicate type (Binder Checkpoint, Decision B.4) and should never "
          "reach physical column translation");
  }
  throw std::runtime_error("Unhandled ExpressionType in ToTypeId");
}

[[nodiscard]] std::vector<gistdb::TypeId> PhysicalOutputSchema(const LogicalPlanNode& node) {
  return std::visit(
      [&](const auto& payload) -> std::vector<gistdb::TypeId> {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, LogicalScan>) {
          std::vector<gistdb::TypeId> types;
          for (std::uint32_t ordinal : payload.required_ordinals) {
            types.push_back(ToTypeId(payload.output_columns[ordinal].type));  // NOLINT
          }
          return types;
        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          return PhysicalOutputSchema(*payload.input);
        } else if constexpr (std::is_same_v<T, LogicalJoin>) {
          auto types = PhysicalOutputSchema(*payload.right);
          auto probe_types = PhysicalOutputSchema(*payload.left);
          types.insert(types.end(), probe_types.begin(), probe_types.end());
          return types;
        } else {
          throw std::runtime_error(
              "PhysicalOutputSchema: Aggregate/Projection as a Join operand is unsupported -- "
              "no subquery-in-FROM support yet (Binder Checkpoint, Decision B.16)");
        }
      },
      node.node);
}

[[nodiscard]] std::size_t FindColumnPosition(const LogicalPlanNode& node,
                                             const BoundColumnRef& ref) {
  return std::visit(
      [&](const auto& payload) -> std::size_t {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, LogicalScan>) {
          if (payload.binding_id != ref.table_id) {
            throw std::runtime_error(
                "FindColumnPosition: column's table_id doesn't match this scan");
          }
          auto it = std::find(payload.required_ordinals.begin(), payload.required_ordinals.end(),
                              ref.ordinal);
          if (it == payload.required_ordinals.end()) {
            throw std::runtime_error(
                "FindColumnPosition: column was pruned but is still referenced -- Column "
                "Pruning bug, not a user error");
          }
          return static_cast<std::size_t>(std::distance(payload.required_ordinals.begin(), it));
        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          return FindColumnPosition(*payload.input, ref);
        } else if constexpr (std::is_same_v<T, LogicalJoin>) {
          std::vector<std::uint32_t> left_ids = gistdb::binder::CollectBindingIds(*payload.left);
          if (std::find(left_ids.begin(), left_ids.end(), ref.table_id) != left_ids.end()) {
            return PhysicalOutputSchema(*payload.right).size() +
                   FindColumnPosition(*payload.left, ref);
          }
          return FindColumnPosition(*payload.right, ref);
        } else if constexpr (std::is_same_v<T, LogicalAggregate>) {
          if (ref.table_id != gistdb::binder::kAggregateOutputBindingId) {
            throw std::runtime_error(
                "FindColumnPosition: a raw table column cannot be resolved above a "
                "LogicalAggregate -- only its own computed output columns are reachable here");
          }
          return ref.ordinal;
        } else {
          throw std::runtime_error(
              "FindColumnPosition: Projection as a Join operand is unsupported -- no "
              "subquery-in-FROM support yet (Binder Checkpoint, Decision B.16)");
        }
      },
      node.node);
}

void RewriteColumnRefsToPhysicalPositions(gistdb::execution::BoundExpression& expr,
                                          const LogicalPlanNode& child) {
  std::visit(
      [&](auto& node) {
        using N = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<N, BoundColumnRef>) {
          node.ordinal = static_cast<std::uint32_t>(FindColumnPosition(child, node));
        } else if constexpr (std::is_same_v<N, gistdb::execution::BinaryOpNode>) {
          RewriteColumnRefsToPhysicalPositions(*node.left, child);
          RewriteColumnRefsToPhysicalPositions(*node.right, child);
        } else if constexpr (std::is_same_v<N, gistdb::execution::UnaryOpNode>) {
          RewriteColumnRefsToPhysicalPositions(*node.operand, child);
        }
      },
      expr.node);
}

[[nodiscard]] std::optional<gistdb::execution::ZoneMapSkipCondition> TryExtractZoneMapSkip(
    const gistdb::execution::BoundExpression& predicate, const LogicalScan& scan) {
  const auto* bin = std::get_if<gistdb::execution::BinaryOpNode>(&predicate.node);
  if (bin == nullptr) {
    return std::nullopt;
  }

  const auto* col_ref = std::get_if<BoundColumnRef>(&bin->left->node);
  const auto* const_node = std::get_if<gistdb::execution::ConstNode>(&bin->right->node);
  if (col_ref == nullptr || const_node == nullptr || col_ref->table_id != scan.binding_id) {
    return std::nullopt;
  }

  return std::visit(
      [&](const auto& value) -> std::optional<gistdb::execution::ZoneMapSkipCondition> {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, std::int32_t> || std::is_same_v<V, float>) {
          return gistdb::execution::ZoneMapSkipCondition{col_ref->ordinal, bin->op, value};
        } else {
          return std::nullopt;
        }
      },
      const_node->value);
}

[[nodiscard]] std::unique_ptr<gistdb::execution::Operator> Translate(  // NOLINT
    std::unique_ptr<LogicalPlanNode> node, gistdb::catalog::Catalog& catalog,
    gistdb::storage::BufferPoolManager& buffer_pool) {
  return std::visit(
      [&](auto& payload) -> std::unique_ptr<gistdb::execution::Operator> {  // NOLINT
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, LogicalScan>) {
          const gistdb::catalog::TableObject* table =
              catalog.GetTableById(payload.physical_table_id);
          if (table == nullptr) {
            throw std::runtime_error("Optimizer::Translate: unknown table_id");
          }
          return std::make_unique<gistdb::execution::SeqScanOperator>(
              *table, payload.required_ordinals, buffer_pool, std::nullopt);

        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          if (const auto* scan = std::get_if<LogicalScan>(&payload.input->node)) {
            auto skip = TryExtractZoneMapSkip(*payload.predicate, *scan);
            const gistdb::catalog::TableObject* table =
                catalog.GetTableById(scan->physical_table_id);
            if (table == nullptr) {
              throw std::runtime_error("Optimizer::Translate: unknown table_id");
            }
            RewriteColumnRefsToPhysicalPositions(*payload.predicate, *payload.input);
            auto scan_op = std::make_unique<gistdb::execution::SeqScanOperator>(
                *table, scan->required_ordinals, buffer_pool, skip);
            return std::make_unique<gistdb::execution::FilterOperator>(
                std::move(scan_op), std::move(payload.predicate));
          }
          RewriteColumnRefsToPhysicalPositions(*payload.predicate, *payload.input);
          auto child = Translate(std::move(payload.input), catalog, buffer_pool);
          return std::make_unique<gistdb::execution::FilterOperator>(std::move(child),
                                                                     std::move(payload.predicate));

        } else if constexpr (std::is_same_v<T, LogicalJoin>) {
          std::vector<std::uint32_t> left_ids = gistdb::binder::CollectBindingIds(*payload.left);

          std::vector<std::uint32_t> probe_key_ordinals;  // left = probe
          std::vector<std::uint32_t> build_key_ordinals;  // right = build
          for (const auto& [key_a, key_b] : payload.equi_conditions) {
            const bool a_is_left =
                std::find(left_ids.begin(), left_ids.end(), key_a.table_id) != left_ids.end();
            const BoundColumnRef& left_key = a_is_left ? key_a : key_b;
            const BoundColumnRef& right_key = a_is_left ? key_b : key_a;
            probe_key_ordinals.push_back(
                static_cast<std::uint32_t>(FindColumnPosition(*payload.left, left_key)));
            build_key_ordinals.push_back(
                static_cast<std::uint32_t>(FindColumnPosition(*payload.right, right_key)));
          }

          std::vector<gistdb::TypeId> build_column_types = PhysicalOutputSchema(*payload.right);

          auto probe_child = Translate(std::move(payload.left), catalog, buffer_pool);
          auto build_child = Translate(std::move(payload.right), catalog, buffer_pool);
          return std::make_unique<gistdb::execution::HashJoinOperator>(
              std::move(build_child), std::move(probe_child), std::move(build_key_ordinals),
              std::move(probe_key_ordinals), std::move(build_column_types));

        } else if constexpr (std::is_same_v<T, LogicalAggregate>) {
          std::vector<std::uint32_t> group_by_positions;
          std::vector<gistdb::TypeId> group_by_types;
          for (const auto& ref : payload.group_by) {
            group_by_positions.push_back(
                static_cast<std::uint32_t>(FindColumnPosition(*payload.input, ref)));
            group_by_types.push_back(ref.type);
          }

          std::vector<gistdb::execution::AggregateSpec> specs;
          for (const auto& agg : payload.aggregates) {
            gistdb::execution::AggregateFunctionKind kind{};
            switch (agg.function) {
              case gistdb::binder::AggregateFunctionKind::kCountStar:
                kind = gistdb::execution::AggregateFunctionKind::kCountStar;
                break;
              case gistdb::binder::AggregateFunctionKind::kCount:
                kind = gistdb::execution::AggregateFunctionKind::kCount;
                break;
              case gistdb::binder::AggregateFunctionKind::kSum:
                kind = gistdb::execution::AggregateFunctionKind::kSum;
                break;
              case gistdb::binder::AggregateFunctionKind::kAvg:
                kind = gistdb::execution::AggregateFunctionKind::kAvg;
                break;
              case gistdb::binder::AggregateFunctionKind::kMin:
                kind = gistdb::execution::AggregateFunctionKind::kMin;
                break;
              case gistdb::binder::AggregateFunctionKind::kMax:
                kind = gistdb::execution::AggregateFunctionKind::kMax;
                break;
            }
            std::optional<std::uint32_t> arg_col;
            gistdb::TypeId arg_type = gistdb::TypeId::kInteger;
            if (agg.argument.has_value()) {
              arg_col =
                  static_cast<std::uint32_t>(FindColumnPosition(*payload.input, *agg.argument));
              arg_type = agg.argument->type;
            }
            specs.push_back(gistdb::execution::AggregateSpec{kind, arg_col, arg_type});
          }

          auto child = Translate(std::move(payload.input), catalog, buffer_pool);
          return std::make_unique<gistdb::execution::AggregationOperator>(
              std::move(child), std::move(group_by_positions), std::move(group_by_types),
              std::move(specs));

        } else {
          for (auto& expr : payload.select_expressions) {
            RewriteColumnRefsToPhysicalPositions(*expr, *payload.input);
          }
          auto child = Translate(std::move(payload.input), catalog, buffer_pool);
          return std::make_unique<gistdb::execution::ProjectionOperator>(
              std::move(child), std::move(payload.select_expressions));
        }
      },
      node->node);
}

}  // namespace

std::unique_ptr<gistdb::execution::Operator> Optimizer::Optimize(
    std::unique_ptr<LogicalPlanNode> root, gistdb::catalog::Catalog& catalog,
    gistdb::storage::BufferPoolManager& buffer_pool) {
  root = PushdownPredicates(std::move(root));
  PruneColumns(*root);
  return Translate(std::move(root), catalog, buffer_pool);
}

}  // namespace gistdb::optimizer