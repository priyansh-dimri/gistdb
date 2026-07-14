#include "gistdb/optimizer/predicate_pushdown.hpp"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <utility>
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
using gistdb::execution::BoundExpression;

using ConjunctList = std::vector<std::unique_ptr<BoundExpression>>;

std::unique_ptr<LogicalPlanNode> Rewrite(std::unique_ptr<LogicalPlanNode> node);
std::unique_ptr<LogicalPlanNode> PlaceInto(std::unique_ptr<LogicalPlanNode> node,
                                           ConjunctList conjuncts);

[[nodiscard]] bool ReferencesOnlyGroupByColumns(const BoundExpression& expr,
                                                const std::vector<BoundColumnRef>& group_by) {
  std::vector<BoundColumnRef> refs = CollectColumnRefs(expr);
  return std::all_of(refs.begin(), refs.end(), [&](const BoundColumnRef& ref) {
    return std::any_of(group_by.begin(), group_by.end(), [&](const BoundColumnRef& key) {
      return key.table_id == ref.table_id && key.ordinal == ref.ordinal;
    });
  });
}

std::unique_ptr<LogicalPlanNode> WrapInFilter(std::unique_ptr<LogicalPlanNode> node,
                                              ConjunctList conjuncts) {
  if (conjuncts.empty()) {
    return node;
  }
  return gistdb::binder::MakeLogicalFilter(std::move(node),
                                           RebuildConjunction(std::move(conjuncts)));
}

std::unique_ptr<LogicalPlanNode> PlaceInto(std::unique_ptr<LogicalPlanNode> node,  // NOLINT
                                           ConjunctList conjuncts) {
  if (conjuncts.empty()) {
    return node;
  }

  return std::visit(
      [&](auto& payload) -> std::unique_ptr<LogicalPlanNode> {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, LogicalScan>) {
          return WrapInFilter(std::move(node), std::move(conjuncts));

        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          ConjunctList combined;
          combined.push_back(std::move(payload.predicate));
          for (auto& conjunct : conjuncts) {
            combined.push_back(std::move(conjunct));
          }
          payload.predicate = RebuildConjunction(std::move(combined));
          return std::move(node);

        } else if constexpr (std::is_same_v<T, LogicalJoin>) {
          std::vector<std::uint32_t> left_ids = gistdb::binder::CollectBindingIds(*payload.left);
          std::vector<std::uint32_t> right_ids = gistdb::binder::CollectBindingIds(*payload.right);

          ConjunctList left_bucket;
          ConjunctList right_bucket;
          ConjunctList stays_here;
          for (auto& conjunct : conjuncts) {
            if (AllColumnsAvailable(*conjunct, left_ids)) {
              left_bucket.push_back(std::move(conjunct));
            } else if (AllColumnsAvailable(*conjunct, right_ids)) {
              right_bucket.push_back(std::move(conjunct));
            } else {
              stays_here.push_back(std::move(conjunct));
            }
          }
          payload.left = PlaceInto(std::move(payload.left), std::move(left_bucket));
          payload.right = PlaceInto(std::move(payload.right), std::move(right_bucket));
          return WrapInFilter(std::move(node), std::move(stays_here));

        } else if constexpr (std::is_same_v<T, LogicalAggregate>) {
          ConjunctList eligible;
          ConjunctList ineligible;
          for (auto& conjunct : conjuncts) {
            if (ReferencesOnlyGroupByColumns(*conjunct, payload.group_by)) {
              eligible.push_back(std::move(conjunct));
            } else {
              ineligible.push_back(std::move(conjunct));
            }
          }
          payload.input = PlaceInto(std::move(payload.input), std::move(eligible));
          return WrapInFilter(std::move(node), std::move(ineligible));

        } else {
          payload.input = PlaceInto(std::move(payload.input), std::move(conjuncts));
          return std::move(node);
        }
      },
      node->node);
}

std::unique_ptr<LogicalPlanNode> Rewrite(std::unique_ptr<LogicalPlanNode> node) {
  return std::visit(
      [&](auto& payload) -> std::unique_ptr<LogicalPlanNode> {
        using T = std::decay_t<decltype(payload)>;

        if constexpr (std::is_same_v<T, LogicalScan>) {
          return std::move(node);

        } else if constexpr (std::is_same_v<T, LogicalFilter>) {
          auto rewritten_child = Rewrite(std::move(payload.input));
          ConjunctList conjuncts = ExtractConjuncts(std::move(payload.predicate));
          return PlaceInto(std::move(rewritten_child), std::move(conjuncts));

        } else if constexpr (std::is_same_v<T, LogicalJoin>) {
          payload.left = Rewrite(std::move(payload.left));
          payload.right = Rewrite(std::move(payload.right));
          return std::move(node);

        } else if constexpr (std::is_same_v<T, LogicalAggregate>) {  // NOLINT
          payload.input = Rewrite(std::move(payload.input));
          return std::move(node);

        } else {
          payload.input = Rewrite(std::move(payload.input));
          return std::move(node);
        }
      },
      node->node);
}

}  // namespace

std::unique_ptr<LogicalPlanNode> PushdownPredicates(std::unique_ptr<LogicalPlanNode> root) {
  return Rewrite(std::move(root));
}

}  // namespace gistdb::optimizer