#pragma once

#include <optional>

#include "gistdb/execution/data_chunk.hpp"

namespace gistdb::execution {

class Operator {
 public:
  Operator() = default;
  virtual ~Operator() = default;

  Operator(const Operator&) = delete;
  Operator& operator=(const Operator&) = delete;
  Operator(Operator&&) = delete;
  Operator& operator=(Operator&&) = delete;

  [[nodiscard]] virtual std::optional<DataChunk> GetNext() = 0;
};

}  // namespace gistdb::execution