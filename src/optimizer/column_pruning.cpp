#include "gistdb/optimizer/column_pruning.hpp"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

#include "gistdb/optimizer/predicate_utils.hpp"

namespace gistdb::optimizer {

namespace {

using gistdb::binder::LogicalAggregate;
using gistdb::binder::LogicalFilter;
using gistdb::binder::LogicalJoin;
using gistdb::binder::LogicalPlanNode;
using gistdb::binder::LogicalProjection;
using gistdb::binder::LogicalScan;
using gistdb::execution::BoundColumnRef;

void Propagate(LogicalPlanNode& node,  // NOLINT
               const std::vector<BoundColumnRef>& required_from_above) {
  std::visit(
      [&](auto& payload) {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, LogicalScan>) {
          std::vector<std::uint32_t> ordinals;
          for (const auto& ref : required_from_above) {
            if (ref.table_id == payload.binding_id) {
              ordinals.push_back(ref.ordinal);
            }
          }
          std::sort(ordinals.begin(), ordinals.end());
          ordinals.erase(std::unique(ordinals.begin(), ordinals.end()), ordinals.end());
          payload.required_ordinals = std::move(ordinals);

        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          std::vector<BoundColumnRef> required = required_from_above;
          std::vector<BoundColumnRef> from_predicate = CollectColumnRefs(*payload.predicate);
          required.insert(required.end(), from_predicate.begin(), from_predicate.end());
          Propagate(*payload.input, required);

        } else if constexpr (std::is_same_v<T, LogicalJoin>) {
          std::vector<BoundColumnRef> required = required_from_above;
          for (const auto& [key_a, key_b] : payload.equi_conditions) {
            required.push_back(key_a);
            required.push_back(key_b);
          }
          std::vector<std::uint32_t> left_ids = gistdb::binder::CollectBindingIds(*payload.left);
          std::vector<std::uint32_t> right_ids = gistdb::binder::CollectBindingIds(*payload.right);

          std::vector<BoundColumnRef> for_left;
          std::vector<BoundColumnRef> for_right;
          for (const auto& ref : required) {
            if (std::find(left_ids.begin(), left_ids.end(), ref.table_id) != left_ids.end()) {
              for_left.push_back(ref);
            } else if (std::find(right_ids.begin(), right_ids.end(), ref.table_id) !=
                       right_ids.end()) {
              for_right.push_back(ref);
            }
          }
          Propagate(*payload.left, for_left);
          Propagate(*payload.right, for_right);

        } else if constexpr (std::is_same_v<T, LogicalAggregate>) {
          std::vector<BoundColumnRef> required = payload.group_by;
          for (const auto& agg : payload.aggregates) {
            if (agg.argument.has_value()) {
              required.push_back(*agg.argument);
            }
          }
          Propagate(*payload.input, required);

        } else {
          std::vector<BoundColumnRef> required;
          for (const auto& expr : payload.select_expressions) {
            std::vector<BoundColumnRef> refs = CollectColumnRefs(*expr);
            required.insert(required.end(), refs.begin(), refs.end());
          }
          Propagate(*payload.input, required);
        }
      },
      node.node);
}

}  // namespace

void PruneColumns(LogicalPlanNode& root) {
  Propagate(root, {});
}

}  // namespace gistdb::optimizer