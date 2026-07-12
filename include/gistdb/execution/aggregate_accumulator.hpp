#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "gistdb/storage/zone_map.hpp"

namespace gistdb::execution {

class CountStarAccumulator {
 public:
  void Add();
  [[nodiscard]] std::int64_t Count() const { return count_; }

 private:
  std::int64_t count_ = 0;
};

class CountAccumulator {
 public:
  void Add(bool is_null);
  [[nodiscard]] std::int64_t Count() const { return count_; }

 private:
  std::int64_t count_ = 0;
};

class SumIntAccumulator {
 public:
  void Add(std::int32_t value, bool is_null);
  [[nodiscard]] bool HasValues() const { return has_values_; }
  [[nodiscard]] std::int64_t Sum() const { return sum_; }  // pre: HasValues()

 private:
  bool has_values_ = false;
  std::int64_t sum_ = 0;
};

class SumFloatAccumulator {
 public:
  void Add(float value, bool is_null);
  [[nodiscard]] bool HasValues() const { return has_values_; }
  [[nodiscard]] double Sum() const { return sum_; }  // pre: HasValues()

 private:
  bool has_values_ = false;
  double sum_ = 0.0;
};

class AvgIntAccumulator {
 public:
  void Add(std::int32_t value, bool is_null);
  [[nodiscard]] bool HasValues() const { return count_ > 0; }
  // pre: HasValues()
  [[nodiscard]] double Average() const {
    return static_cast<double>(sum_) / static_cast<double>(count_);
  }

 private:
  std::int64_t sum_ = 0;
  std::int64_t count_ = 0;
};

class AvgFloatAccumulator {
 public:
  void Add(float value, bool is_null);
  [[nodiscard]] bool HasValues() const { return count_ > 0; }
  [[nodiscard]] double Average() const {
    return sum_ / static_cast<double>(count_);
  }  // pre: HasValues()

 private:
  double sum_ = 0.0;
  std::int64_t count_ = 0;
};

template <typename T>
class MinMaxAccumulator {
 public:
  void Add(T value, bool is_null);
  [[nodiscard]] bool HasValues() const { return zone_map_.HasValues(); }
  [[nodiscard]] T Min() const { return zone_map_.Min(); }  // pre: HasValues()
  [[nodiscard]] T Max() const { return zone_map_.Max(); }  // pre: HasValues()

 private:
  gistdb::storage::ZoneMap<T> zone_map_;
};

extern template class MinMaxAccumulator<std::int32_t>;
extern template class MinMaxAccumulator<float>;

class MinMaxVarcharAccumulator {
 public:
  void Add(std::string_view value, bool is_null);
  [[nodiscard]] bool HasValues() const { return has_values_; }
  [[nodiscard]] const std::string& Min() const { return min_; }  // pre: HasValues()
  [[nodiscard]] const std::string& Max() const { return max_; }  // pre: HasValues()

 private:
  bool has_values_ = false;
  std::string min_;
  std::string max_;
};

}  // namespace gistdb::execution