#pragma once

#include <cstdint>
#include <type_traits>
#include <variant>

#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/page_range.hpp"
#include "gistdb/storage/varchar_column.hpp"
#include "gistdb/storage/zone_map.hpp"

namespace gistdb::storage {
template <typename T>
class FixedWidthColumnFooterEntry {
  static_assert(std::is_same_v<T, std::int32_t> || std::is_same_v<T, float>,
                "supports only INTEGER (int32) and FLOAT (float32)");

 public:
  // Scans `column` once, computing null_count and the zone map
  static FixedWidthColumnFooterEntry Build(const FixedWidthColumn<T>& column, PageRange pages);

  // Reconstructs an entry from the already known field values. It is used when
  // deserializing a footer, where the original column no longer exists.
  static FixedWidthColumnFooterEntry FromFields(PageRange pages, std::uint32_t null_count,
                                                ZoneMap<T> zone);

  [[nodiscard]] PageRange Pages() const { return pages_; }
  [[nodiscard]] std::uint32_t NullCount() const { return null_count_; }
  [[nodiscard]] const ZoneMap<T>& Zone() const { return zone_map_; }

 private:
  PageRange pages_;
  std::uint32_t null_count_ = 0;
  ZoneMap<T> zone_map_;
};

extern template class FixedWidthColumnFooterEntry<std::int32_t>;
extern template class FixedWidthColumnFooterEntry<float>;

class VarcharColumnFooterEntry {
 public:
  static VarcharColumnFooterEntry Build(const VarcharColumn& column, PageRange offsets_pages,
                                        PageRange data_pages);

  static VarcharColumnFooterEntry FromFields(PageRange offsets_pages, PageRange data_pages,
                                             std::uint32_t null_count, VarcharZoneMap zone);

  [[nodiscard]] PageRange OffsetsPages() const { return offsets_pages_; }
  [[nodiscard]] PageRange DataPages() const { return data_pages_; }
  [[nodiscard]] std::uint32_t NullCount() const { return null_count_; }
  [[nodiscard]] const VarcharZoneMap& Zone() const { return zone_map_; }

 private:
  PageRange offsets_pages_;
  PageRange data_pages_;
  std::uint32_t null_count_ = 0;
  VarcharZoneMap zone_map_;
};

// Unifying type: one footer entry for one column, whatever its type.
// Used by RowGroupFooterEntry to hold a mixed-type column list.
using ColumnFooterEntry =
    std::variant<FixedWidthColumnFooterEntry<std::int32_t>, FixedWidthColumnFooterEntry<float>,
                 VarcharColumnFooterEntry>;

}  // namespace gistdb::storage