#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gistdb/catalog/column_def.hpp"
#include "gistdb/storage/row_group_footer_entry.hpp"

namespace gistdb::catalog {

class TableObject {
 public:
  TableObject(std::uint32_t table_id, std::string table_name, std::vector<ColumnDef> columns);

  std::uint32_t TableId() const { return table_id_; }
  const std::string& TableName() const { return table_name_; }

  std::size_t NumColumns() const { return columns_.size(); }
  const ColumnDef& Column(std::size_t ordinal) const;

  const ColumnDef* FindColumn(const std::string& name) const;
  void AddRowGroup(gistdb::storage::RowGroupFooterEntry row_group);

  const std::vector<gistdb::storage::RowGroupFooterEntry>& RowGroups() const { return row_groups_; }
  std::uint64_t TotalRowCount() const { return total_row_count_; }

 private:
  std::uint32_t table_id_;
  std::string table_name_;
  std::vector<ColumnDef> columns_;
  std::unordered_map<std::string, std::uint32_t> column_ordinal_map_;

  std::vector<gistdb::storage::RowGroupFooterEntry> row_groups_;
  std::uint64_t total_row_count_ = 0;
};

}  // namespace gistdb::catalog