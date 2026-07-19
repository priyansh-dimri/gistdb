// include/gistdb/cli/driver.hpp
#pragma once

#include <ostream>
#include <string>

#include "gistdb/catalog/catalog.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"

namespace gistdb::cli {
class Driver {
 public:
  Driver(gistdb::catalog::Catalog& catalog, gistdb::storage::BufferPoolManager& buffer_pool,
         std::ostream& out);
  void ExecuteStatement(const std::string& sql);

 private:
  gistdb::catalog::Catalog& catalog_;                // NOLINT
  gistdb::storage::BufferPoolManager& buffer_pool_;  // NOLINT
  std::ostream& out_;                                // NOLINT
  [[nodiscard]] bool TryHandleMetaCommand(const std::string& input);
};

}  // namespace gistdb::cli