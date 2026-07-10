#include "gistdb/storage/zone_map.hpp"
#include "gistdb/constants.hpp"
#include <algorithm>

namespace gistdb::storage {

template <typename T> void ZoneMap<T>::Update(T value) {
  if (!has_values_) {
    min_ = value;
    max_ = value;
    has_values_ = true;
    return;
  }
  min_ = std::min(min_, value);
  max_ = std::max(max_, value);
}

template <typename T> T ZoneMap<T>::Min() const { return min_; }

template <typename T> T ZoneMap<T>::Max() const { return max_; }

template class ZoneMap<std::int32_t>;
template class ZoneMap<float>;

namespace {
std::string TruncatePrefix(std::string_view value) {
  return std::string(
      value.substr(0, std::min(value.size(), kZoneMapPrefixLength)));
}
} // namespace

void VarcharZoneMap::Update(std::string_view value) {
  std::string prefix = TruncatePrefix(value);
  if (!has_values_) {
    min_prefix_ = prefix;
    max_prefix_ = prefix;
    has_values_ = true;
    return;
  }
  min_prefix_ = std::min(min_prefix_, prefix);
  max_prefix_ = std::max(max_prefix_, prefix);
}

std::string_view VarcharZoneMap::MinPrefix() const { return min_prefix_; }
std::string_view VarcharZoneMap::MaxPrefix() const { return max_prefix_; }

} // namespace gistdb::storage