#include "gistdb/catalog/catalog.hpp"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "gistdb/serialization/byte_io.hpp"
#include "gistdb/storage/footer.hpp"

namespace gistdb::catalog {

namespace {

std::vector<std::uint8_t> CombineSchemaAndFooter(const SchemaSection& schema,
                                                 const gistdb::storage::Footer& footer) {
  std::vector<std::uint8_t> schema_bytes = schema.Serialize();
  std::vector<std::uint8_t> footer_bytes = footer.Serialize();

  std::vector<std::uint8_t> combined;
  gistdb::serialization::WriteU32(combined, static_cast<std::uint32_t>(schema_bytes.size()));
  combined.insert(combined.end(), schema_bytes.begin(), schema_bytes.end());
  combined.insert(combined.end(), footer_bytes.begin(), footer_bytes.end());
  return combined;
}

struct SplitBlob {
  SchemaSection schema;
  gistdb::storage::Footer footer;
};

SplitBlob SplitSchemaAndFooter(const std::vector<std::uint8_t>& combined) {
  gistdb::serialization::ByteReader reader(combined);
  std::uint32_t schema_length = reader.ReadU32();

  std::vector<std::uint8_t> schema_bytes(combined.begin() + 4,
                                         combined.begin() + 4 + schema_length);
  std::vector<std::uint8_t> footer_bytes(combined.begin() + 4 + schema_length, combined.end());

  return SplitBlob{
      .schema = SchemaSection::Deserialize(schema_bytes),
      .footer = gistdb::storage::Footer::Deserialize(footer_bytes),
  };
}

}  // namespace

Catalog::Catalog(gistdb::storage::DiskManager disk_manager)
    : disk_manager_(std::move(disk_manager)) {}

Catalog Catalog::CreateNew(const std::filesystem::path& path) {
  gistdb::storage::DiskManager disk_manager = gistdb::storage::DiskManager::CreateNew(path);
  Catalog catalog(std::move(disk_manager));
  catalog.PersistMetadata();
  return catalog;
}

Catalog Catalog::Open(const std::filesystem::path& path) {
  gistdb::storage::DiskManager disk_manager = gistdb::storage::DiskManager::Open(path);
  Catalog catalog(std::move(disk_manager));
  catalog.ReconstructFromDisk();
  return catalog;
}

void Catalog::ReconstructFromDisk() {
  std::vector<std::uint8_t> combined = disk_manager_.ReadMetadataBlob();
  SplitBlob split = SplitSchemaAndFooter(combined);
  schema_section_ = std::move(split.schema);

  std::unordered_map<std::uint32_t, TableObject*> table_by_id;
  for (std::size_t i = 0; i < schema_section_.NumTables(); ++i) {
    const TableSchemaEntry& entry = schema_section_.Table(i);
    auto insert_result = tables_by_name_.emplace(
        entry.table_name, TableObject(entry.table_id, entry.table_name, entry.columns));
    table_by_id.emplace(entry.table_id, &insert_result.first->second);
  }

  for (std::size_t i = 0; i < split.footer.NumRowGroups(); ++i) {
    const gistdb::storage::RowGroupFooterEntry& row_group = split.footer.RowGroup(i);
    auto it = table_by_id.find(row_group.TableId());
    if (it == table_by_id.end()) {
      throw std::runtime_error(
          "Catalog::Open: row group references an unknown table_id -- corrupt metadata");
    }
    it->second->AddRowGroup(row_group);
  }
}

void Catalog::PersistMetadata() {
  gistdb::storage::Footer footer;
  for (const auto& entry : tables_by_name_) {
    for (const auto& row_group : entry.second.RowGroups()) {
      footer.AddRowGroup(row_group);
    }
  }
  disk_manager_.WriteMetadataBlob(CombineSchemaAndFooter(schema_section_, footer));
}

std::uint32_t Catalog::CreateTable(const std::string& table_name, std::vector<ColumnDef> columns) {
  if (tables_by_name_.contains(table_name)) {
    throw std::invalid_argument("Catalog::CreateTable: table '" + table_name + "' already exists");
  }
  std::uint32_t table_id = schema_section_.AddTable(table_name, columns);
  tables_by_name_.emplace(table_name, TableObject(table_id, table_name, std::move(columns)));
  PersistMetadata();
  return table_id;
}

const TableObject* Catalog::GetTable(const std::string& table_name) const {
  auto it = tables_by_name_.find(table_name);
  if (it == tables_by_name_.end()) {
    return nullptr;
  }
  return &it->second;
}

const TableObject* Catalog::GetTableById(std::uint32_t table_id) const {
  for (const auto& [name, table] : tables_by_name_) {
    if (table.TableId() == table_id) {
      return &table;
    }
  }
  return nullptr;
}

void Catalog::Flush() {
  PersistMetadata();
}

Catalog::~Catalog() {
  try {
    PersistMetadata();
  } catch (...) {  // NOLINT
  }
}

void Catalog::AddRowGroup(const std::string& table_name,
                          gistdb::storage::RowGroupFooterEntry row_group) {
  auto it = tables_by_name_.find(table_name);
  if (it == tables_by_name_.end()) {
    throw std::invalid_argument("Catalog::AddRowGroup: unknown table '" + table_name + "'");
  }
  it->second.AddRowGroup(std::move(row_group));
  // PersistMetadata();
}

}  // namespace gistdb::catalog