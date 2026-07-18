#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "gistdb/storage/validity_bitmap.hpp"

namespace gistdb::storage {

template <typename T>
class FixedWidthColumn {
  static_assert(std::is_same_v<T, std::int32_t> || std::is_same_v<T, float>,
                "FixedWidthColumn supports only INTEGER (int32) and FLOAT (float32)");

 public:
  FixedWidthColumn() = default;

  void Append(T value);
  void AppendNull();

  [[nodiscard]] T GetValue(std::size_t index) const;
  void SetValue(std::size_t index, T value);
  void SetNull(std::size_t index, bool is_null);

  [[nodiscard]] bool IsNull(std::size_t index) const;
  [[nodiscard]] bool IsValid(std::size_t index) const;

  [[nodiscard]] std::size_t Size() const;

  [[nodiscard]] const T* Data() const;  // zero-copy read path
  [[nodiscard]] const ValidityBitmap& Validity() const;

 private:
  std::vector<T> values_;
  ValidityBitmap validity_{0};
};

extern template class FixedWidthColumn<std::int32_t>;
extern template class FixedWidthColumn<float>;

}  // namespace gistdb::storage