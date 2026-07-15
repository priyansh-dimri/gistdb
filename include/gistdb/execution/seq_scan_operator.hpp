#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "gistdb/catalog/table_object.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/execution/operator.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"

namespace gistdb::execution {

struct ZoneMapSkipCondition {
  std::uint32_t ordinal;
  BinaryOperator op;
  std::variant<std::int32_t, float> constant;
};

class SeqScanOperator : public Operator {
 public:
  SeqScanOperator(const gistdb::catalog::TableObject& table,
                  std::vector<std::uint32_t> required_ordinals,
                  gistdb::storage::BufferPoolManager& buffer_pool,
                  std::optional<ZoneMapSkipCondition> zone_map_skip = std::nullopt);
  ~SeqScanOperator() override;

  SeqScanOperator(const SeqScanOperator&) = delete;
  SeqScanOperator& operator=(const SeqScanOperator&) = delete;
  SeqScanOperator(SeqScanOperator&&) noexcept;
  SeqScanOperator& operator=(SeqScanOperator&&) noexcept;

  [[nodiscard]] std::optional<DataChunk> GetNext() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gistdb::execution