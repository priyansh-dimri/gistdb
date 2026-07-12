#include "gistdb/execution/filter_operator.hpp"

#include <utility>

#include "gistdb/execution/expression_evaluator.hpp"

namespace gistdb::execution {

FilterOperator::FilterOperator(std::unique_ptr<Operator> child,
                               std::unique_ptr<BoundExpression> predicate)
    : child_(std::move(child)), predicate_(std::move(predicate)) {}

std::optional<DataChunk> FilterOperator::GetNext() {
  std::optional<DataChunk> chunk = child_->GetNext();
  if (!chunk.has_value()) {
    return std::nullopt;
  }

  EvaluationResult predicate_result = ExpressionEvaluator::Evaluate(*predicate_, *chunk);
  const auto& bool_result = std::get<BooleanResult>(predicate_result);

  for (std::uint32_t i = 0; i < chunk->RowCount(); ++i) {
    bool passes =
        chunk->IsRowSelected(i) && bool_result.values.IsValid(i) && bool_result.validity.IsValid(i);
    chunk->SetRowSelected(i, passes);
  }

  return chunk;
}

}  // namespace gistdb::execution