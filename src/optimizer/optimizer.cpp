#include "gistdb/optimizer/optimizer.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "gistdb/execution/filter_operator.hpp"
#include "gistdb/execution/hash_join_operator.hpp"
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
        } else {
          throw std::runtime_error(
              "FindColumnPosition: Aggregate/Projection as a Join operand is unsupported -- no "
              "subquery-in-FROM support yet (Binder Checkpoint, Decision B.16)");
        }
      },
      node.node);
}

[[nodiscard]] std::unique_ptr<gistdb::execution::Operator> Translate(
    std::unique_ptr<LogicalPlanNode> node) {
  return std::visit(
      [&](auto& payload) -> std::unique_ptr<gistdb::execution::Operator> {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, LogicalScan>) {
          throw std::runtime_error(
              "LogicalScan -> SeqScan translation is a deliberate stub (SeqScan doesn't exist "
              "yet -- see this sub-morsel's own note on Phase 6)");

        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          auto child = Translate(std::move(payload.input));
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

          auto probe_child = Translate(std::move(payload.left));
          auto build_child = Translate(std::move(payload.right));
          return std::make_unique<gistdb::execution::HashJoinOperator>(
              std::move(build_child), std::move(probe_child), std::move(build_key_ordinals),
              std::move(probe_key_ordinals), std::move(build_column_types));

        } else if constexpr (std::is_same_v<T, LogicalAggregate>) {
          throw std::runtime_error(
              "LogicalAggregate -> Aggregation operator translation is stubbed -- no "
              "aggregation_operator.hpp was available; see this response's note");

        } else {
          throw std::runtime_error(
              "LogicalProjection -> Projection operator translation is stubbed -- no "
              "projection_operator.hpp was available; see this response's note");
        }
      },
      node->node);
}

}  // namespace

std::unique_ptr<gistdb::execution::Operator> Optimizer::Optimize(
    std::unique_ptr<LogicalPlanNode> root) {
  root = PushdownPredicates(std::move(root));
  PruneColumns(*root);
  return Translate(std::move(root));
}

}  // namespace gistdb::optimizer