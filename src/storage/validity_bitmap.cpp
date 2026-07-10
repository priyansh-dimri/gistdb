#include "gistdb/storage/validity_bitmap.hpp"

namespace gistdb::storage {
namespace {
constexpr std::size_t kBitsPerByte = 8;
}

ValidityBitmap::ValidityBitmap(std::size_t num_rows, bool default_valid)
    : num_rows_(num_rows),
      bytes_((num_rows + kBitsPerByte - 1) / kBitsPerByte,
             default_valid ? std::uint8_t{0xFF} : std::uint8_t{0x00}) {}

void ValidityBitmap::SetValid(std::size_t index, bool is_valid) {
  std::size_t byte_index = index / kBitsPerByte;
  auto bit_mask = static_cast<std::uint8_t>(1u << (index % kBitsPerByte));
  if (is_valid) {
    bytes_[byte_index] |= bit_mask;
  } else {
    bytes_[byte_index] &= static_cast<std::uint8_t>(~bit_mask);
  }
}

bool ValidityBitmap::IsValid(std::size_t index) const {
  std::size_t byte_index = index / kBitsPerByte;
  auto bit_mask = static_cast<std::uint8_t>(1u << (index % kBitsPerByte));
  return (bytes_[byte_index] & bit_mask) != 0;
}

bool ValidityBitmap::IsNull(std::size_t index) const { return !IsValid(index); }

void ValidityBitmap::PushBack(bool is_valid) {
  ++num_rows_;
  std::size_t needed_bytes = (num_rows_ + kBitsPerByte - 1) / kBitsPerByte;
  if (bytes_.size() < needed_bytes) {
    bytes_.resize(needed_bytes, std::uint8_t{0x00});
  }
  SetValid(num_rows_ - 1, is_valid);
}

std::size_t ValidityBitmap::Size() const { return num_rows_; }

std::size_t ValidityBitmap::CountNulls() const {
  std::size_t count = 0;
  for (std::size_t i = 0; i < num_rows_; ++i) {
    count += IsNull(i) ? 1 : 0;
  }
  return count;
}

const std::uint8_t *ValidityBitmap::Data() const { return bytes_.data(); }

std::size_t ValidityBitmap::ByteSize() const { return bytes_.size(); }

} // namespace gistdb::storage