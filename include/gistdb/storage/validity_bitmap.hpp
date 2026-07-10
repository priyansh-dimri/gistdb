#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gistdb::storage {

class ValidityBitmap {
public:
  // Constructs a bitmap for `num_rows` rows, all set to default_valid.
  explicit ValidityBitmap(std::size_t num_rows, bool default_valid = true);

  void SetValid(std::size_t index, bool is_valid);
  bool IsValid(std::size_t index) const;
  bool IsNull(std::size_t index) const;

  // Grows the bitmap by one row
  void PushBack(bool is_valid);

  std::size_t Size() const;
  std::size_t CountNulls() const;

  const std::uint8_t *Data() const;
  std::size_t ByteSize() const;

private:
  std::size_t num_rows_;
  std::vector<std::uint8_t> bytes_;
};

} // namespace gistdb::storage