#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gistdb/catalog/column_def.hpp"

namespace gistdb::catalog {

struct TableSchemaEntry {
  std::uint32_t table_id;
  std::string table_name;
  std::vector<ColumnDef> columns;

  friend bool operator==(const TableSchemaEntry&, const TableSchemaEntry&) = default;
};

class SchemaSection {
 public:
  SchemaSection() = default;

  [[nodiscard]] std::uint32_t NextTableId() const { return next_table_id_; }

  std::uint32_t AddTable(std::string table_name, std::vector<ColumnDef> columns);

  [[nodiscard]] std::size_t NumTables() const { return tables_.size(); }
  [[nodiscard]] const TableSchemaEntry& Table(std::size_t index) const;  // pre: index < NumTables()

  [[nodiscard]] std::vector<std::uint8_t> Serialize() const;
  static SchemaSection Deserialize(const std::vector<std::uint8_t>& bytes);

 private:
  std::uint32_t next_table_id_ = 0;
  std::vector<TableSchemaEntry> tables_;
};

}  // namespace gistdb::catalog