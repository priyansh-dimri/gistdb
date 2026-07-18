#pragma once

#include <memory>

#include "gistdb/binder/logical_plan.hpp"

namespace gistdb::optimizer {
[[nodiscard]] std::unique_ptr<gistdb::binder::LogicalPlanNode> PushdownPredicates(
    std::unique_ptr<gistdb::binder::LogicalPlanNode> root);

}  // namespace gistdb::optimizer