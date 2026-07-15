// include/gistdb/execution/aggregation_operator.hpp
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

// Ordinal-based, deliberately not depending on gistdb::binder at all --
// same reasoning as HashJoinOperator's plain-ordinal constructor (Optimizer
// Checkpoint, Decision 18's translation is where BoundColumnRef/binder's
// own AggregateFunctionKind get resolved down to this; that translation
// isn't wired up yet -- Optimizer::Translate's LogicalAggregate branch is
// still the stub from last turn).
struct AggregateSpec {  // NOLINT
  AggregateFunctionKind function;
  std::optional<std::uint32_t> argument_column;  // index into child's
                                                 // DataChunk; nullopt iff
                                                 // kCountStar (Decision
                                                 // B.13/B.V.17)
  gistdb::TypeId argument_type;                  // meaningful only if argument_column is set
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

  // First call fully drains `child` and builds every group (Decision
  // B.I.2/B.V.19); every call after that serves one already-computed
  // 1,024-row-or-fewer batch of output rows until exhausted.
  [[nodiscard]] std::optional<DataChunk> GetNext() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gistdb::execution