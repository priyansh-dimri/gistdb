#include "gistdb/storage/footer.hpp"

#include <cstring>
#include <type_traits>
#include <utility>

#include "gistdb/constants.hpp"

namespace gistdb::storage {

namespace {

constexpr std::uint8_t kColumnTagInteger = 0;
constexpr std::uint8_t kColumnTagFloat = 1;
constexpr std::uint8_t kColumnTagVarchar = 2;

void WriteU8(std::vector<std::uint8_t>& buf, std::uint8_t value) {
  buf.push_back(value);
}

void WriteU32(std::vector<std::uint8_t>& buf, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

void WriteFloat(std::vector<std::uint8_t>& buf, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  WriteU32(buf, bits);
}

void WritePageRange(std::vector<std::uint8_t>& buf, PageRange range) {
  WriteU32(buf, range.start_page_id);
  WriteU32(buf, range.page_count);
}

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
    WriteFloat(buf, value);
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

class ByteReader {
 public:
  explicit ByteReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  std::uint8_t ReadU8() { return bytes_[pos_++]; }

  std::uint32_t ReadU32() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(bytes_[pos_++]) << (8 * i);
    }
    return value;
  }

  float ReadFloat() {
    std::uint32_t bits = ReadU32();
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  PageRange ReadPageRange() {
    PageRange range;
    range.start_page_id = ReadU32();
    range.page_count = ReadU32();
    return range;
  }

  std::string ReadFixedPrefix() {
    std::uint8_t length = ReadU8();
    std::string result(kZoneMapPrefixLength, '\0');
    for (std::size_t i = 0; i < kZoneMapPrefixLength; ++i) {
      result[i] = static_cast<char>(ReadU8());
    }
    result.resize(length);
    return result;
  }

 private:
  const std::vector<std::uint8_t>&
      bytes_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  std::size_t pos_ = 0;
};

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
  PageRange pages = reader.ReadPageRange();
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
  PageRange offsets_pages = reader.ReadPageRange();
  PageRange data_pages = reader.ReadPageRange();
  std::uint32_t null_count = reader.ReadU32();
  bool has_values = reader.ReadU8() != 0;
  VarcharZoneMap zone;
  if (has_values) {
    std::string min_prefix = reader.ReadFixedPrefix();
    std::string max_prefix = reader.ReadFixedPrefix();
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
    PageRange validity_region = reader.ReadPageRange();
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