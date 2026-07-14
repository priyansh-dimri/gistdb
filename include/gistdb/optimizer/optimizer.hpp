#pragma once

#include <memory>

#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/execution/operator.hpp"

namespace gistdb::optimizer {

class Optimizer {
 public:
  [[nodiscard]] static std::unique_ptr<gistdb::execution::Operator> Optimize(
      std::unique_ptr<gistdb::binder::LogicalPlanNode> root);
};

}  // namespace gistdb::optimizer