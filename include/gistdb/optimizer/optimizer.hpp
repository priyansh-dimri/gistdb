#pragma once

#include <memory>

#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/execution/operator.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"

namespace gistdb::optimizer {

class Optimizer {
 public:
  [[nodiscard]] static std::unique_ptr<gistdb::execution::Operator> Optimize(
      std::unique_ptr<gistdb::binder::LogicalPlanNode> root, gistdb::catalog::Catalog& catalog,
      gistdb::storage::BufferPoolManager& buffer_pool);
};

}  // namespace gistdb::optimizer