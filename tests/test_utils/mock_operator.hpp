#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/execution/operator.hpp"

namespace gistdb::test_utils {

class MockOperator : public gistdb::execution::Operator {
 public:
  explicit MockOperator(std::vector<gistdb::execution::DataChunk> chunks)
      : chunks_(std::move(chunks)) {}

  std::optional<gistdb::execution::DataChunk> GetNext() override {
    if (next_index_ >= chunks_.size()) {
      return std::nullopt;
    }
    return std::move(chunks_[next_index_++]);
  }

 private:
  std::vector<gistdb::execution::DataChunk> chunks_;
  std::size_t next_index_ = 0;
};

}  // namespace gistdb::test_utils