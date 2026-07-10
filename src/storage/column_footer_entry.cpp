#include "gistdb/storage/column_footer_entry.hpp"

namespace gistdb::storage {

template <typename T>
FixedWidthColumnFooterEntry<T>
FixedWidthColumnFooterEntry<T>::Build(const FixedWidthColumn<T> &column,
                                      PageRange pages) {
  FixedWidthColumnFooterEntry<T> entry;
  entry.pages_ = pages;
  for (std::size_t i = 0; i < column.Size(); ++i) {
    if (column.IsNull(i)) {
      ++entry.null_count_;
    } else {
      entry.zone_map_.Update(column.GetValue(i));
    }
  }
  return entry;
}

template class FixedWidthColumnFooterEntry<std::int32_t>;
template class FixedWidthColumnFooterEntry<float>;

VarcharColumnFooterEntry
VarcharColumnFooterEntry::Build(const VarcharColumn &column,
                                PageRange offsets_pages, PageRange data_pages) {
  VarcharColumnFooterEntry entry;
  entry.offsets_pages_ = offsets_pages;
  entry.data_pages_ = data_pages;
  for (std::size_t i = 0; i < column.Size(); ++i) {
    if (column.IsNull(i)) {
      ++entry.null_count_;
    } else {
      entry.zone_map_.Update(column.GetValue(i));
    }
  }
  return entry;
}

} // namespace gistdb::storage