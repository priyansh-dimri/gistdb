// include/gistdb/optimizer/predicate_pushdown.hpp
#pragma once

#include <memory>

#include "gistdb/binder/logical_plan.hpp"

namespace gistdb::optimizer {

// Decisions 7, 8, 10. Walks the tree exactly once (Decision 4), dissolving
// every existing LogicalFilter and relocating its conjuncts as far toward
// their LogicalScan as each conjunct's own referenced columns allow. A
// Join's own equi-condition is never touched (Decision 8); a conjunct may
// only cross below an Aggregate if it references exclusively that
// Aggregate's group-by columns (Decision 10).
//
// "In-place" (Decision 5) here means move-based reconstruction of the same
// node chain -- ownership is threaded through via std::move throughout,
// never copied into a parallel tree, but the actual C++ mechanism is
// reassignment through unique_ptr, not literal pointer mutation.
[[nodiscard]] std::unique_ptr<gistdb::binder::LogicalPlanNode> PushdownPredicates(
    std::unique_ptr<gistdb::binder::LogicalPlanNode> root);

}  // namespace gistdb::optimizer