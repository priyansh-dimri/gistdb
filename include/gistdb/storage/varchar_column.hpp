#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "gistdb/storage/validity_bitmap.hpp"

namespace gistdb::storage {
class VarcharColumn {
public:
  VarcharColumn();

  void Append(std::string_view value);
  void AppendNull();

  std::string_view GetValue(std::size_t index) const; // pre: !IsNull(index)

  bool IsNull(std::size_t index) const;
  bool IsValid(std::size_t index) const;

  std::size_t Size() const;

  const std::uint8_t *DataBuffer() const;
  std::size_t DataBufferSize() const;

  const std::int32_t *
  Offsets() const; // size() == Size() + 1, Offsets()[0] == 0
  std::size_t NumOffsets() const;

  const ValidityBitmap &Validity() const;

private:
  std::vector<std::uint8_t> data_buffer_;
  std::vector<std::int32_t> offsets_;
  ValidityBitmap validity_{0};
};
} // namespace gistdb::storage