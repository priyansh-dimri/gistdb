#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "gistdb/catalog/column_def.hpp"
#include "gistdb/catalog/schema_section.hpp"
#include "gistdb/catalog/table_object.hpp"
#include "gistdb/storage/disk_manager.hpp"
#include "gistdb/storage/row_group_footer_entry.hpp"

namespace gistdb::catalog {
class Catalog {
 public:
  static Catalog CreateNew(const std::filesystem::path& path);
  static Catalog Open(const std::filesystem::path& path);
  std::uint32_t CreateTable(const std::string& table_name, std::vector<ColumnDef> columns);

  // Returns nullptr if no such table exists.
  const TableObject* GetTable(const std::string& table_name) const;
  const TableObject* GetTableById(std::uint32_t table_id) const;

  void AddRowGroup(const std::string& table_name, gistdb::storage::RowGroupFooterEntry row_group);
  gistdb::storage::DiskManager& GetDiskManager() { return disk_manager_; }

  void Flush();

  ~Catalog();
  Catalog(Catalog&&) noexcept = default;
  Catalog& operator=(Catalog&&) noexcept = default;
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;

 private:
  explicit Catalog(gistdb::storage::DiskManager disk_manager);

  void ReconstructFromDisk();
  void PersistMetadata();

  gistdb::storage::DiskManager disk_manager_;
  SchemaSection schema_section_;
  std::unordered_map<std::string, TableObject> tables_by_name_;
};

}  // namespace gistdb::catalog