#pragma once

#include <optional>

#include "gistdb/execution/data_chunk.hpp"

namespace gistdb::execution {

class Operator {  // NOLINT
 public:
  virtual ~Operator() = default;

  [[nodiscard]] virtual std::optional<DataChunk> GetNext() = 0;
};
}  // namespace gistdb::execution