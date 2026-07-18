#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::optimizer {

using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
void FlattenConjuncts(const BoundExpression& expr, std::vector<const BoundExpression*>& out);
[[nodiscard]] std::vector<const BoundExpression*> FlattenConjuncts(const BoundExpression& expr);
void ExtractConjuncts(std::unique_ptr<BoundExpression> expr,
                      std::vector<std::unique_ptr<BoundExpression>>& out);
[[nodiscard]] std::vector<std::unique_ptr<BoundExpression>> ExtractConjuncts(
    std::unique_ptr<BoundExpression> expr);

[[nodiscard]] std::unique_ptr<BoundExpression> RebuildConjunction(
    std::vector<std::unique_ptr<BoundExpression>> conjuncts);

void CollectColumnRefs(const BoundExpression& expr, std::vector<BoundColumnRef>& out);
[[nodiscard]] std::vector<BoundColumnRef> CollectColumnRefs(const BoundExpression& expr);

[[nodiscard]] bool AllColumnsAvailable(const BoundExpression& expr,
                                       const std::vector<std::uint32_t>& available_table_ids);

}  // namespace gistdb::optimizer