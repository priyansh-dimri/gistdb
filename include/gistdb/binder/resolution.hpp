#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gistdb/binder/ast.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::binder {

class ResolutionException : public std::runtime_error {
 public:
  explicit ResolutionException(const std::string& message) : std::runtime_error(message) {}
};

struct ResolvedTableBinding {
  std::uint32_t binding_id;
  std::string binding_name;
  const gistdb::catalog::TableObject* table;
};

class ResolutionScope {
 public:
  std::uint32_t RegisterTable(const std::string& table_name,
                              const std::optional<std::string>& alias,
                              const gistdb::catalog::Catalog& catalog);

  [[nodiscard]] gistdb::execution::BoundColumnRef Resolve(const ColumnRefNode& column_ref) const;

  [[nodiscard]] const std::vector<ResolvedTableBinding>& Bindings() const { return bindings_; }
  [[nodiscard]] const ResolvedTableBinding& BindingFor(std::uint32_t binding_id) const;

 private:
  std::vector<ResolvedTableBinding> bindings_;
};

}  // namespace gistdb::binder