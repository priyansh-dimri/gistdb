#include "gistdb/optimizer/predicate_utils.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace gistdb::optimizer {

using gistdb::execution::BinaryOperator;
using gistdb::execution::BinaryOpNode;
using gistdb::execution::UnaryOpNode;

void FlattenConjuncts(const BoundExpression& expr, std::vector<const BoundExpression*>& out) {
  if (const auto* bin = std::get_if<BinaryOpNode>(&expr.node);
      bin != nullptr && bin->op == BinaryOperator::kAnd) {
    FlattenConjuncts(*bin->left, out);
    FlattenConjuncts(*bin->right, out);
  } else {
    out.push_back(&expr);
  }
}

std::vector<const BoundExpression*> FlattenConjuncts(const BoundExpression& expr) {
  std::vector<const BoundExpression*> out;
  FlattenConjuncts(expr, out);
  return out;
}

void ExtractConjuncts(std::unique_ptr<BoundExpression> expr,
                      std::vector<std::unique_ptr<BoundExpression>>& out) {
  if (auto* bin = std::get_if<BinaryOpNode>(&expr->node);
      bin != nullptr && bin->op == BinaryOperator::kAnd) {
    ExtractConjuncts(std::move(bin->left), out);
    ExtractConjuncts(std::move(bin->right), out);
  } else {
    out.push_back(std::move(expr));
  }
}

std::vector<std::unique_ptr<BoundExpression>> ExtractConjuncts(
    std::unique_ptr<BoundExpression> expr) {
  std::vector<std::unique_ptr<BoundExpression>> out;
  ExtractConjuncts(std::move(expr), out);
  return out;
}

std::unique_ptr<BoundExpression> RebuildConjunction(
    std::vector<std::unique_ptr<BoundExpression>> conjuncts) {
  std::unique_ptr<BoundExpression> result = std::move(conjuncts[0]);
  for (std::size_t i = 1; i < conjuncts.size(); ++i) {
    result = gistdb::execution::MakeBooleanOp(BinaryOperator::kAnd, std::move(result),
                                              std::move(conjuncts[i]));
  }
  return result;
}

void CollectColumnRefs(const BoundExpression& expr, std::vector<BoundColumnRef>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, BoundColumnRef>) {
          out.push_back(node);
        } else if constexpr (std::is_same_v<T, BinaryOpNode>) {
          CollectColumnRefs(*node.left, out);
          CollectColumnRefs(*node.right, out);
        } else if constexpr (std::is_same_v<T, UnaryOpNode>) {
          CollectColumnRefs(*node.operand, out);
        }
      },
      expr.node);
}

std::vector<BoundColumnRef> CollectColumnRefs(const BoundExpression& expr) {
  std::vector<BoundColumnRef> out;
  CollectColumnRefs(expr, out);
  return out;
}

bool AllColumnsAvailable(const BoundExpression& expr,
                         const std::vector<std::uint32_t>& available_table_ids) {
  std::vector<BoundColumnRef> refs = CollectColumnRefs(expr);
  return std::all_of(refs.begin(), refs.end(), [&](const BoundColumnRef& ref) {
    return std::find(available_table_ids.begin(), available_table_ids.end(), ref.table_id) !=
           available_table_ids.end();
  });
}

}  // namespace gistdb::optimizer