#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::storage {
VarcharColumn::VarcharColumn() : offsets_{0} {}
void VarcharColumn::Append(std::string_view value) {
  data_buffer_.insert(data_buffer_.end(), value.begin(), value.end());
  offsets_.push_back(static_cast<std::int32_t>(data_buffer_.size()));
  validity_.PushBack(true);
}

void VarcharColumn::AppendNull() {
  offsets_.push_back(static_cast<std::int32_t>(data_buffer_.size()));
  validity_.PushBack(false);
}

std::string_view VarcharColumn::GetValue(std::size_t index) const {
  auto start = static_cast<std::size_t>(offsets_[index]);
  auto end = static_cast<std::size_t>(offsets_[index + 1]);
  return {reinterpret_cast<const char*>(data_buffer_.data()) + start, end - start};
}

bool VarcharColumn::IsNull(std::size_t index) const {
  return validity_.IsNull(index);
}
bool VarcharColumn::IsValid(std::size_t index) const {
  return validity_.IsValid(index);
}

std::size_t VarcharColumn::Size() const {
  return offsets_.size() - 1;
}

const std::uint8_t* VarcharColumn::DataBuffer() const {
  return data_buffer_.data();
}
std::size_t VarcharColumn::DataBufferSize() const {
  return data_buffer_.size();
}

const std::int32_t* VarcharColumn::Offsets() const {
  return offsets_.data();
}
std::size_t VarcharColumn::NumOffsets() const {
  return offsets_.size();
}

const ValidityBitmap& VarcharColumn::Validity() const {
  return validity_;
}

}  // namespace gistdb::storage