#pragma once

#include <cstdint>
#include <vector>

#include "gistdb/storage/column_footer_entry.hpp"
#include "gistdb/storage/page_range.hpp"

namespace gistdb::storage {

// One row group's footer metadata.
// - row_count
// - table_id
// - validity_bitmap_region: (all of this row group's columns' validity bitmaps,
//   packed into one shared region rather than a page range per column)
class RowGroupFooterEntry {
 public:
  RowGroupFooterEntry(std::uint32_t table_id, std::uint32_t row_count,
                      PageRange validity_bitmap_region, std::vector<ColumnFooterEntry> columns);

  [[nodiscard]] std::uint32_t TableId() const { return table_id_; }
  [[nodiscard]] std::uint32_t RowCount() const { return row_count_; }
  [[nodiscard]] PageRange ValidityBitmapRegion() const { return validity_bitmap_region_; }

  [[nodiscard]] std::size_t NumColumns() const { return columns_.size(); }
  [[nodiscard]] const ColumnFooterEntry& Column(std::size_t ordinal) const;  // pre: ordinal < NumColumns()

 private:
  std::uint32_t table_id_;
  std::uint32_t row_count_;
  PageRange validity_bitmap_region_;
  std::vector<ColumnFooterEntry> columns_;
};

}  // namespace gistdb::storage