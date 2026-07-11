#include "gistdb/storage/footer.hpp"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gistdb/constants.hpp"
#include "gistdb/serialization/byte_io.hpp"

namespace gistdb::storage {

namespace {

using gistdb::serialization::ByteReader;
using gistdb::serialization::WriteU32;
using gistdb::serialization::WriteU8;

constexpr std::uint8_t kColumnTagInteger = 0;
constexpr std::uint8_t kColumnTagFloat = 1;
constexpr std::uint8_t kColumnTagVarchar = 2;

void WritePageRange(std::vector<std::uint8_t>& buf, PageRange range) {
  WriteU32(buf, range.start_page_id);
  WriteU32(buf, range.page_count);
}

// length byte + fixed, zero-padded kZoneMapPrefixLength-byte buffer.
void WriteFixedPrefix(std::vector<std::uint8_t>& buf, std::string_view data) {
  WriteU8(buf, static_cast<std::uint8_t>(data.size()));
  for (std::size_t i = 0; i < kZoneMapPrefixLength; ++i) {
    buf.push_back(i < data.size() ? static_cast<std::uint8_t>(data[i]) : std::uint8_t{0});
  }
}

template <typename T>
void WriteScalar(std::vector<std::uint8_t>& buf, T value) {
  if constexpr (std::is_same_v<T, std::int32_t>) {
    WriteU32(buf, static_cast<std::uint32_t>(value));
  } else {
    gistdb::serialization::WriteFloat(buf, value);
  }
}

template <typename T>
void WriteFixedWidthEntry(std::vector<std::uint8_t>& buf,
                          const FixedWidthColumnFooterEntry<T>& entry) {
  WritePageRange(buf, entry.Pages());
  WriteU32(buf, entry.NullCount());
  WriteU8(buf, entry.Zone().HasValues() ? 1 : 0);
  if (entry.Zone().HasValues()) {
    WriteScalar<T>(buf, entry.Zone().Min());
    WriteScalar<T>(buf, entry.Zone().Max());
  }
}

void WriteVarcharEntry(std::vector<std::uint8_t>& buf, const VarcharColumnFooterEntry& entry) {
  WritePageRange(buf, entry.OffsetsPages());
  WritePageRange(buf, entry.DataPages());
  WriteU32(buf, entry.NullCount());
  WriteU8(buf, entry.Zone().HasValues() ? 1 : 0);
  if (entry.Zone().HasValues()) {
    WriteFixedPrefix(buf, entry.Zone().MinPrefix());
    WriteFixedPrefix(buf, entry.Zone().MaxPrefix());
  }
}

void WriteColumn(std::vector<std::uint8_t>& buf, const ColumnFooterEntry& column) {
  std::visit(
      [&buf](const auto& entry) {
        using EntryType = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<EntryType, FixedWidthColumnFooterEntry<std::int32_t>>) {
          WriteU8(buf, kColumnTagInteger);
          WriteFixedWidthEntry(buf, entry);
        } else if constexpr (std::is_same_v<EntryType, FixedWidthColumnFooterEntry<float>>) {
          WriteU8(buf, kColumnTagFloat);
          WriteFixedWidthEntry(buf, entry);
        } else {
          WriteU8(buf, kColumnTagVarchar);
          WriteVarcharEntry(buf, entry);
        }
      },
      column);
}

PageRange ReadPageRange(ByteReader& reader) {
  PageRange range;
  range.start_page_id = reader.ReadU32();
  range.page_count = reader.ReadU32();
  return range;
}

std::string ReadFixedPrefixValue(ByteReader& reader) {
  std::uint8_t length = reader.ReadU8();
  std::string value = reader.ReadFixedBytes(kZoneMapPrefixLength);
  value.resize(length);
  return value;
}

template <typename T>
T ReadScalar(ByteReader& reader) {
  if constexpr (std::is_same_v<T, std::int32_t>) {
    return static_cast<std::int32_t>(reader.ReadU32());
  } else {
    return reader.ReadFloat();
  }
}

template <typename T>
FixedWidthColumnFooterEntry<T> ReadFixedWidthEntry(ByteReader& reader) {
  PageRange pages = ReadPageRange(reader);
  std::uint32_t null_count = reader.ReadU32();
  bool has_values = reader.ReadU8() != 0;
  ZoneMap<T> zone;
  if (has_values) {
    T min_value = ReadScalar<T>(reader);
    T max_value = ReadScalar<T>(reader);
    zone.Update(min_value);
    zone.Update(max_value);
  }
  return FixedWidthColumnFooterEntry<T>::FromFields(pages, null_count, zone);
}

VarcharColumnFooterEntry ReadVarcharEntry(ByteReader& reader) {
  PageRange offsets_pages = ReadPageRange(reader);
  PageRange data_pages = ReadPageRange(reader);
  std::uint32_t null_count = reader.ReadU32();
  bool has_values = reader.ReadU8() != 0;
  VarcharZoneMap zone;
  if (has_values) {
    std::string min_prefix = ReadFixedPrefixValue(reader);
    std::string max_prefix = ReadFixedPrefixValue(reader);
    zone.Update(min_prefix);
    zone.Update(max_prefix);
  }
  return VarcharColumnFooterEntry::FromFields(offsets_pages, data_pages, null_count, zone);
}

ColumnFooterEntry ReadColumn(ByteReader& reader) {
  std::uint8_t tag = reader.ReadU8();
  switch (tag) {
    case kColumnTagInteger:
      return ColumnFooterEntry{ReadFixedWidthEntry<std::int32_t>(reader)};
    case kColumnTagFloat:
      return ColumnFooterEntry{ReadFixedWidthEntry<float>(reader)};
    default:
      return ColumnFooterEntry{ReadVarcharEntry(reader)};
  }
}

}  // namespace

void Footer::AddRowGroup(RowGroupFooterEntry entry) {
  row_groups_.push_back(std::move(entry));
}

std::size_t Footer::NumRowGroups() const {
  return row_groups_.size();
}

const RowGroupFooterEntry& Footer::RowGroup(std::size_t index) const {
  return row_groups_[index];
}

std::vector<std::uint8_t> Footer::Serialize() const {
  std::vector<std::uint8_t> buf;
  WriteU32(buf, static_cast<std::uint32_t>(row_groups_.size()));
  for (const auto& row_group : row_groups_) {
    WriteU32(buf, row_group.TableId());
    WriteU32(buf, row_group.RowCount());
    WritePageRange(buf, row_group.ValidityBitmapRegion());
    WriteU32(buf, static_cast<std::uint32_t>(row_group.NumColumns()));
    for (std::size_t i = 0; i < row_group.NumColumns(); ++i) {
      WriteColumn(buf, row_group.Column(i));
    }
  }
  return buf;
}

Footer Footer::Deserialize(const std::vector<std::uint8_t>& bytes) {
  ByteReader reader(bytes);
  Footer footer;
  std::uint32_t num_row_groups = reader.ReadU32();
  for (std::uint32_t rg = 0; rg < num_row_groups; ++rg) {
    std::uint32_t table_id = reader.ReadU32();
    std::uint32_t row_count = reader.ReadU32();
    PageRange validity_region = ReadPageRange(reader);
    std::uint32_t num_columns = reader.ReadU32();
    std::vector<ColumnFooterEntry> columns;
    columns.reserve(num_columns);
    for (std::uint32_t c = 0; c < num_columns; ++c) {
      columns.push_back(ReadColumn(reader));
    }
    footer.AddRowGroup(
        RowGroupFooterEntry(table_id, row_count, validity_region, std::move(columns)));
  }
  return footer;
}

}  // namespace gistdb::storage