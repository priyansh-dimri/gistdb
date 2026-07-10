#include "gistdb/storage/row_group_footer_entry.hpp"
#include <utility>

namespace gistdb::storage {

RowGroupFooterEntry::RowGroupFooterEntry(std::uint32_t table_id,
                                         std::uint32_t row_count,
                                         PageRange validity_bitmap_region,
                                         std::vector<ColumnFooterEntry> columns)
    : table_id_(table_id), row_count_(row_count),
      validity_bitmap_region_(validity_bitmap_region),
      columns_(std::move(columns)) {}

const ColumnFooterEntry &
RowGroupFooterEntry::Column(std::size_t ordinal) const {
  return columns_[ordinal];
}

} // namespace gistdb::storage