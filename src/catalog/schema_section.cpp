#include "gistdb/catalog/schema_section.hpp"

#include <stdexcept>
#include <utility>

#include "gistdb/serialization/byte_io.hpp"

namespace gistdb::catalog {

namespace {

using gistdb::serialization::ByteReader;
using gistdb::serialization::WriteString;
using gistdb::serialization::WriteU32;
using gistdb::serialization::WriteU8;

void WriteColumn(std::vector<std::uint8_t>& buf, const ColumnDef& column) {
  WriteString(buf, column.name);
  WriteU8(buf, static_cast<std::uint8_t>(column.type));
  WriteU32(buf, column.ordinal);
}

gistdb::TypeId ReadTypeId(ByteReader& reader) {
  std::uint8_t raw = reader.ReadU8();
  if (raw > static_cast<std::uint8_t>(gistdb::TypeId::kVarchar)) {
    throw std::runtime_error("SchemaSection::Deserialize: invalid TypeId byte");
  }
  return static_cast<gistdb::TypeId>(raw);
}

ColumnDef ReadColumn(ByteReader& reader) {
  std::string name = reader.ReadString();
  gistdb::TypeId type = ReadTypeId(reader);
  std::uint32_t ordinal = reader.ReadU32();
  return ColumnDef{
      .name = std::move(name),
      .type = type,
      .ordinal = ordinal,
  };
}

}  // namespace

std::uint32_t SchemaSection::AddTable(std::string table_name, std::vector<ColumnDef> columns) {
  std::uint32_t table_id = next_table_id_;
  ++next_table_id_;
  tables_.push_back(TableSchemaEntry{
      .table_id = table_id,
      .table_name = std::move(table_name),
      .columns = std::move(columns),
  });
  return table_id;
}

const TableSchemaEntry& SchemaSection::Table(std::size_t index) const {
  return tables_[index];
}

std::vector<std::uint8_t> SchemaSection::Serialize() const {
  std::vector<std::uint8_t> buf;
  WriteU32(buf, next_table_id_);
  WriteU32(buf, static_cast<std::uint32_t>(tables_.size()));
  for (const auto& table : tables_) {
    WriteU32(buf, table.table_id);
    WriteString(buf, table.table_name);
    WriteU32(buf, static_cast<std::uint32_t>(table.columns.size()));
    for (const auto& column : table.columns) {
      WriteColumn(buf, column);
    }
  }
  return buf;
}

SchemaSection SchemaSection::Deserialize(const std::vector<std::uint8_t>& bytes) {
  ByteReader reader(bytes);
  SchemaSection schema;
  schema.next_table_id_ = reader.ReadU32();

  std::uint32_t num_tables = reader.ReadU32();
  schema.tables_.reserve(num_tables);
  for (std::uint32_t t = 0; t < num_tables; ++t) {
    std::uint32_t table_id = reader.ReadU32();
    std::string table_name = reader.ReadString();
    std::uint32_t num_columns = reader.ReadU32();
    std::vector<ColumnDef> columns;
    columns.reserve(num_columns);
    for (std::uint32_t c = 0; c < num_columns; ++c) {
      columns.push_back(ReadColumn(reader));
    }
    schema.tables_.push_back(TableSchemaEntry{
        .table_id = table_id,
        .table_name = std::move(table_name),
        .columns = std::move(columns),
    });
  }
  return schema;
}

}  // namespace gistdb::catalog