#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/execution/operator.hpp"
#include "gistdb/types.hpp"

namespace gistdb::execution {

class HashJoinOperator : public Operator {
 public:
  HashJoinOperator(std::unique_ptr<Operator> build_child, std::unique_ptr<Operator> probe_child,
                   std::vector<std::uint32_t> build_key_ordinals,
                   std::vector<std::uint32_t> probe_key_ordinals,
                   std::vector<gistdb::TypeId> build_column_types);
  ~HashJoinOperator() override;

  HashJoinOperator(const HashJoinOperator&) = delete;
  HashJoinOperator& operator=(const HashJoinOperator&) = delete;
  HashJoinOperator(HashJoinOperator&&) noexcept;
  HashJoinOperator& operator=(HashJoinOperator&&) noexcept;

  [[nodiscard]] std::optional<DataChunk> GetNext() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gistdb::execution