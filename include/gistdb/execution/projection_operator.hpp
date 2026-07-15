#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/execution/operator.hpp"

namespace gistdb::execution {

class ProjectionOperator : public Operator {
 public:
  ProjectionOperator(std::unique_ptr<Operator> child,
                     std::vector<std::unique_ptr<BoundExpression>> expressions);
  ~ProjectionOperator() override;

  ProjectionOperator(const ProjectionOperator&) = delete;
  ProjectionOperator& operator=(const ProjectionOperator&) = delete;
  ProjectionOperator(ProjectionOperator&&) noexcept;
  ProjectionOperator& operator=(ProjectionOperator&&) noexcept;
  [[nodiscard]] std::optional<DataChunk> GetNext() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gistdb::execution