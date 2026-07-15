#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/execution/operator.hpp"
#include "gistdb/types.hpp"

namespace gistdb::execution {

enum class AggregateFunctionKind : std::uint8_t {
  kCountStar,
  kCount,
  kSum,
  kAvg,
  kMin,
  kMax,
};

struct AggregateSpec {  // NOLINT
  AggregateFunctionKind function;
  std::optional<std::uint32_t> argument_column;
  gistdb::TypeId argument_type;
};

class AggregationOperator : public Operator {
 public:
  AggregationOperator(std::unique_ptr<Operator> child, std::vector<std::uint32_t> group_by_columns,
                      std::vector<gistdb::TypeId> group_by_types,
                      std::vector<AggregateSpec> aggregates);
  ~AggregationOperator() override;

  AggregationOperator(const AggregationOperator&) = delete;
  AggregationOperator& operator=(const AggregationOperator&) = delete;
  AggregationOperator(AggregationOperator&&) noexcept;
  AggregationOperator& operator=(AggregationOperator&&) noexcept;

  [[nodiscard]] std::optional<DataChunk> GetNext() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gistdb::execution