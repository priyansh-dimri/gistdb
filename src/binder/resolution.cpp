#include "gistdb/binder/resolution.hpp"

#include <utility>

namespace gistdb::binder {

std::uint32_t ResolutionScope::RegisterTable(const std::string& table_name,
                                             const std::optional<std::string>& alias,
                                             const gistdb::catalog::Catalog& catalog) {
  const gistdb::catalog::TableObject* table = catalog.GetTable(table_name);
  if (table == nullptr) {
    throw ResolutionException("Unknown table: " + table_name);
  }

  std::string binding_name = alias.value_or(table_name);
  for (const auto& existing : bindings_) {
    if (existing.binding_name == binding_name) {
      throw ResolutionException(
          "Duplicate table reference '" + binding_name +
          "' in FROM/JOIN -- a repeated table (self-join) requires distinct aliases");
    }
  }

  const auto binding_id = static_cast<std::uint32_t>(bindings_.size());
  bindings_.push_back(ResolvedTableBinding{
      .binding_id = binding_id, .binding_name = std::move(binding_name), .table = table});
  return binding_id;
}

gistdb::execution::BoundColumnRef ResolutionScope::Resolve(const ColumnRefNode& column_ref) const {
  if (column_ref.table_qualifier.has_value()) {
    const std::string& qualifier = *column_ref.table_qualifier;
    for (const auto& binding : bindings_) {
      if (binding.binding_name == qualifier) {
        const gistdb::catalog::ColumnDef* col = binding.table->FindColumn(column_ref.column_name);
        if (col == nullptr) {
          throw ResolutionException("Column '" + column_ref.column_name + "' not found in '" +
                                    qualifier + "'");
        }
        return gistdb::execution::BoundColumnRef{
            .table_id = binding.binding_id, .ordinal = col->ordinal, .type = col->type};
      }
    }
    throw ResolutionException("Unknown table alias or name: " + qualifier);
  }

  std::optional<gistdb::execution::BoundColumnRef> found;
  for (const auto& binding : bindings_) {
    const gistdb::catalog::ColumnDef* col = binding.table->FindColumn(column_ref.column_name);
    if (col != nullptr) {
      if (found.has_value()) {
        throw ResolutionException("Ambiguous column reference: " + column_ref.column_name);
      }
      found = gistdb::execution::BoundColumnRef{
          .table_id = binding.binding_id, .ordinal = col->ordinal, .type = col->type};
    }
  }
  if (!found.has_value()) {
    throw ResolutionException("Column not found: " + column_ref.column_name);
  }
  return *found;
}

const ResolvedTableBinding& ResolutionScope::BindingFor(std::uint32_t binding_id) const {
  return bindings_.at(binding_id);
}

}  // namespace gistdb::binder