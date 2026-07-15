#include "gistdb/execution/projection_operator.hpp"

#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "gistdb/execution/expression_evaluator.hpp"

namespace gistdb::execution {

class ProjectionOperator::Impl {
 public:
  Impl(std::unique_ptr<Operator> child, std::vector<std::unique_ptr<BoundExpression>> expressions)
      : child_(std::move(child)), expressions_(std::move(expressions)) {}

  std::optional<DataChunk> GetNext() {
    std::optional<DataChunk> input = child_->GetNext();
    if (!input.has_value()) {
      return std::nullopt;
    }

    last_batch_.clear();
    last_batch_.reserve(expressions_.size());
    for (const auto& expr : expressions_) {
      last_batch_.push_back(ExpressionEvaluator::Evaluate(*expr, *input));
    }

    DataChunk output(input->RowCount());
    for (std::uint32_t row = 0; row < input->RowCount(); ++row) {
      output.SetRowSelected(row, input->IsRowSelected(row));
    }

    for (auto& result : last_batch_) {
      std::visit(
          [&](auto& column) {
            using T = std::decay_t<decltype(column)>;
            if constexpr (std::is_same_v<T, BooleanResult>) {
              throw std::runtime_error(
                  "Projecting a bare boolean-valued expression is not yet supported -- no "
                  "DataChunk column type or display format exists for it");
            } else {
              output.AddColumn(&column);
            }
          },
          result);
    }
    return output;
  }

 private:
  std::unique_ptr<Operator> child_;
  std::vector<std::unique_ptr<BoundExpression>> expressions_;
  std::vector<EvaluationResult> last_batch_;
};

ProjectionOperator::ProjectionOperator(std::unique_ptr<Operator> child,
                                       std::vector<std::unique_ptr<BoundExpression>> expressions)
    : impl_(std::make_unique<Impl>(std::move(child), std::move(expressions))) {}

ProjectionOperator::~ProjectionOperator() = default;
ProjectionOperator::ProjectionOperator(ProjectionOperator&&) noexcept = default;
ProjectionOperator& ProjectionOperator::operator=(ProjectionOperator&&) noexcept = default;

std::optional<DataChunk> ProjectionOperator::GetNext() {
  return impl_->GetNext();
}

}  // namespace gistdb::execution