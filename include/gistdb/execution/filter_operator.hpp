#pragma once

#include <memory>
#include <optional>

#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/execution/operator.hpp"

namespace gistdb::execution {

class FilterOperator : public Operator {
 public:
  FilterOperator(std::unique_ptr<Operator> child, std::unique_ptr<BoundExpression> predicate);

  [[nodiscard]] std::optional<DataChunk> GetNext() override;

 private:
  std::unique_ptr<Operator> child_;
  std::unique_ptr<BoundExpression> predicate_;
};

}  // namespace gistdb::execution