#pragma once

#include "gistdb/binder/logical_plan.hpp"

namespace gistdb::optimizer {

void PruneColumns(gistdb::binder::LogicalPlanNode& root);

}  // namespace gistdb::optimizer