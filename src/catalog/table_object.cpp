#include "gistdb/catalog/table_object.hpp"

#include <stdexcept>
#include <utility>

namespace gistdb::catalog {

TableObject::TableObject(std::uint32_t table_id, std::string table_name,
                         std::vector<ColumnDef> columns)
    : table_id_(table_id), table_name_(std::move(table_name)), columns_(std::move(columns)) {
  for (const auto& column : columns_) {
    column_ordinal_map_.emplace(column.name, column.ordinal);
  }
}

const ColumnDef& TableObject::Column(std::size_t ordinal) const {
  return columns_[ordinal];
}

const ColumnDef* TableObject::FindColumn(const std::string& name) const {
  auto it = column_ordinal_map_.find(name);
  if (it == column_ordinal_map_.end()) {
    return nullptr;
  }
  return &columns_[it->second];
}

void TableObject::AddRowGroup(gistdb::storage::RowGroupFooterEntry row_group) {
  if (row_group.TableId() != table_id_) {
    throw std::invalid_argument("TableObject::AddRowGroup: row group's table_id does not match");
  }
  total_row_count_ += row_group.RowCount();
  row_groups_.push_back(std::move(row_group));
}

}  // namespace gistdb::catalog