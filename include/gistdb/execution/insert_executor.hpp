#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gistdb/catalog/catalog.hpp"
#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::execution {

class InsertExecutor {  // NOLINT
 public:
  InsertExecutor(gistdb::catalog::Catalog& catalog, std::uint32_t table_id);
  ~InsertExecutor();

  InsertExecutor(const InsertExecutor&) = delete;
  InsertExecutor& operator=(const InsertExecutor&) = delete;
  void InsertRow(const std::vector<std::unique_ptr<BoundExpression>>& row);
  void Finish();

 private:
  class Staging;

  void FlushRowGroup();

  gistdb::catalog::Catalog& catalog_;
  const gistdb::catalog::TableObject& table_;
  gistdb::storage::DiskManager& disk_manager_;
  std::unique_ptr<Staging> staging_;
};

}  // namespace gistdb::execution