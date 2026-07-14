#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/binder/parser.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::binder {

class BindException : public std::runtime_error {
 public:
  explicit BindException(const std::string& message) : std::runtime_error(message) {}
};

struct BoundInsert {
  std::uint32_t table_id;
  std::vector<std::vector<std::unique_ptr<gistdb::execution::BoundExpression>>> rows;
};

struct TableCreated {
  std::uint32_t table_id;
};

using BindResult = std::variant<std::unique_ptr<LogicalPlanNode>, BoundInsert, TableCreated>;

class Binder {
 public:
  [[nodiscard]] static BindResult Bind(const ParsedStatement& statement,
                                       gistdb::catalog::Catalog& catalog);
};

}  // namespace gistdb::binder