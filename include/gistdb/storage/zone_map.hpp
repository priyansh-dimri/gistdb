#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace gistdb::storage {

template <typename T> class ZoneMap {
  static_assert(std::is_same_v<T, std::int32_t> || std::is_same_v<T, float>,
                "ZoneMap<T> supports only INTEGER (int32) and FLOAT (float32)");

public:
  ZoneMap() = default;

  void Update(T value);

  T Min() const; // pre: HasValues()
  T Max() const; // pre: HasValues()
  bool HasValues() const { return has_values_; }

private:
  bool has_values_ = false;
  T min_{};
  T max_{};
};

extern template class ZoneMap<std::int32_t>;
extern template class ZoneMap<float>;

// VARCHAR zone map: fixed-size truncated prefix (kZoneMapPrefixLength
// bytes), compared lexicographically by byte value.
class VarcharZoneMap {
public:
  VarcharZoneMap() = default;

  void Update(std::string_view value); // truncates internally

  std::string_view MinPrefix() const; // pre: HasValues()
  std::string_view MaxPrefix() const; // pre: HasValues()
  bool HasValues() const { return has_values_; }

private:
  bool has_values_ = false;
  std::string min_prefix_;
  std::string max_prefix_;
};

} // namespace gistdb::storage