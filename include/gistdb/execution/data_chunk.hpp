#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/validity_bitmap.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {

using ColumnView = std::variant<const gistdb::storage::FixedWidthColumn<std::int32_t>*,
                                const gistdb::storage::FixedWidthColumn<float>*,
                                const gistdb::storage::VarcharColumn*>;

class DataChunk {
 public:
  // pre: row_count <= kVectorSize.
  explicit DataChunk(std::uint32_t row_count);

  [[nodiscard]] std::uint32_t RowCount() const { return row_count_; }

  // pre: the pointed-to column's Size() == RowCount().
  void AddColumn(ColumnView column);
  [[nodiscard]] std::size_t NumColumns() const { return columns_.size(); }
  [[nodiscard]] const ColumnView& Column(std::size_t ordinal) const;  // pre: ordinal < NumColumns()

  [[nodiscard]] bool IsRowSelected(std::uint32_t index) const;
  void SetRowSelected(std::uint32_t index, bool selected);
  [[nodiscard]] std::uint32_t CountSelectedRows() const;

 private:
  std::uint32_t row_count_;
  gistdb::storage::ValidityBitmap selection_vector_;
  std::vector<ColumnView> columns_;
};

}  // namespace gistdb::execution