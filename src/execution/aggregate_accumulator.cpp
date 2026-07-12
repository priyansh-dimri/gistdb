#include "gistdb/execution/aggregate_accumulator.hpp"

#include <algorithm>

namespace gistdb::execution {

void CountStarAccumulator::Add() {
  ++count_;
}

void CountAccumulator::Add(bool is_null) {
  if (!is_null) {
    ++count_;
  }
}

void SumIntAccumulator::Add(std::int32_t value, bool is_null) {
  if (is_null) {
    return;
  }
  has_values_ = true;
  sum_ += value;
}

void SumFloatAccumulator::Add(float value, bool is_null) {
  if (is_null) {
    return;
  }
  has_values_ = true;
  sum_ += value;
}

void AvgIntAccumulator::Add(std::int32_t value, bool is_null) {
  if (is_null) {
    return;
  }
  sum_ += value;
  ++count_;
}

void AvgFloatAccumulator::Add(float value, bool is_null) {
  if (is_null) {
    return;
  }
  sum_ += value;
  ++count_;
}

template <typename T>
void MinMaxAccumulator<T>::Add(T value, bool is_null) {
  if (!is_null) {
    zone_map_.Update(value);
  }
}

template class MinMaxAccumulator<std::int32_t>;
template class MinMaxAccumulator<float>;

void MinMaxVarcharAccumulator::Add(std::string_view value, bool is_null) {
  if (is_null) {
    return;
  }
  if (!has_values_) {
    min_ = std::string(value);
    max_ = std::string(value);
    has_values_ = true;
    return;
  }
  min_ = std::min(min_, std::string(value));
  max_ = std::max(max_, std::string(value));
}

}  // namespace gistdb::execution