#include "gistdb/execution/data_chunk.hpp"

#include <stdexcept>
#include <variant>

#include "gistdb/constants.hpp"

namespace gistdb::execution {

DataChunk::DataChunk(std::uint32_t row_count)
    : row_count_(row_count), selection_vector_(row_count, true) {
  if (row_count > gistdb::kVectorSize) {
    throw std::invalid_argument("DataChunk: row_count exceeds kVectorSize");
  }
}

void DataChunk::AddColumn(ColumnView column) {
  std::size_t column_size = std::visit([](const auto* col) { return col->Size(); }, column);
  if (column_size != row_count_) {
    throw std::invalid_argument("DataChunk::AddColumn: column size does not match chunk row count");
  }
  columns_.push_back(column);
}

const ColumnView& DataChunk::Column(std::size_t ordinal) const {
  return columns_[ordinal];
}

bool DataChunk::IsRowSelected(std::uint32_t index) const {
  return selection_vector_.IsValid(index);
}

void DataChunk::SetRowSelected(std::uint32_t index, bool selected) {
  selection_vector_.SetValid(index, selected);
}

std::uint32_t DataChunk::CountSelectedRows() const {
  return row_count_ - static_cast<std::uint32_t>(selection_vector_.CountNulls());
}

}  // namespace gistdb::execution