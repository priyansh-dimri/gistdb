#include "gistdb/execution/expression_evaluator.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::execution {

namespace {

EvaluationResult EvaluateExpr(const BoundExpression& expr, const DataChunk& chunk);

EvaluationResult EvaluateColumnRef(const BoundColumnRef& ref, const DataChunk& chunk) {
  const ColumnView& column = chunk.Column(ref.ordinal);
  return std::visit(
      [](const auto* col) -> EvaluationResult {
        std::decay_t<decltype(*col)> result;
        for (std::size_t i = 0; i < col->Size(); ++i) {
          if (col->IsNull(i)) {
            result.AppendNull();
          } else {
            result.Append(col->GetValue(i));
          }
        }
        return result;
      },
      column);
}

EvaluationResult EvaluateConst(const ConstNode& node, std::size_t row_count) {
  return std::visit(
      [row_count](const auto& value) -> EvaluationResult {
        using ValueType = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<ValueType, std::string>) {
          gistdb::storage::VarcharColumn result;
          for (std::size_t i = 0; i < row_count; ++i) {
            result.Append(value);
          }
          return result;
        } else {
          gistdb::storage::FixedWidthColumn<ValueType> result;
          for (std::size_t i = 0; i < row_count; ++i) {
            result.Append(value);
          }
          return result;
        }
      },
      node.value);
}

bool IsFloatResult(const EvaluationResult& result) {
  return std::holds_alternative<gistdb::storage::FixedWidthColumn<float>>(result);
}

float AsFloat(const EvaluationResult& result, std::size_t i) {
  if (IsFloatResult(result)) {
    return std::get<gistdb::storage::FixedWidthColumn<float>>(result).GetValue(i);
  }
  return static_cast<float>(
      std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result).GetValue(i));
}

std::int32_t AsInt(const EvaluationResult& result, std::size_t i) {
  return std::get<gistdb::storage::FixedWidthColumn<std::int32_t>>(result).GetValue(i);
}

bool IsValidAt(const EvaluationResult& result, std::size_t i) {
  return std::visit(
      [i](const auto& value) -> bool {
        using ValueType = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<ValueType, BooleanResult>) {
          return value.validity.IsValid(i);
        } else {
          return value.IsValid(i);
        }
      },
      result);
}

std::int32_t ApplyIntArithmetic(BinaryOperator op, std::int32_t lhs, std::int32_t rhs) {
  switch (op) {
    case BinaryOperator::kAdd:
      return lhs + rhs;
    case BinaryOperator::kSubtract:
      return lhs - rhs;
    case BinaryOperator::kMultiply:
      return lhs * rhs;
    case BinaryOperator::kDivide:
      // skip the division with a safe placeholder(NULL) here if division by zero happens
      return rhs == 0 ? 0 : lhs / rhs;
    default:
      return 0;
  }
}

float ApplyFloatArithmetic(BinaryOperator op, float lhs, float rhs) {
  switch (op) {
    case BinaryOperator::kAdd:
      return lhs + rhs;
    case BinaryOperator::kSubtract:
      return lhs - rhs;
    case BinaryOperator::kMultiply:
      return lhs * rhs;
    case BinaryOperator::kDivide:
      return lhs / rhs;
    default:
      return 0.0F;
  }
}

EvaluationResult EvaluateArithmeticInt(BinaryOperator op, const EvaluationResult& left,
                                       const EvaluationResult& right, std::size_t row_count) {
  gistdb::storage::FixedWidthColumn<std::int32_t> result;
  // Pass 1: compute every row's raw value unconditionally.
  for (std::size_t i = 0; i < row_count; ++i) {
    result.Append(ApplyIntArithmetic(op, AsInt(left, i), AsInt(right, i)));
  }
  // Pass 2: mask via validity-bitmap AND, as a separate step.
  for (std::size_t i = 0; i < row_count; ++i) {
    bool operands_valid = IsValidAt(left, i) && IsValidAt(right, i);
    bool would_divide_by_zero = op == BinaryOperator::kDivide && AsInt(right, i) == 0;
    if (!operands_valid || would_divide_by_zero) {
      result.SetNull(i, true);
    }
  }
  return result;
}

EvaluationResult EvaluateArithmeticFloat(BinaryOperator op, const EvaluationResult& left,
                                         const EvaluationResult& right, std::size_t row_count) {
  gistdb::storage::FixedWidthColumn<float> result;
  for (std::size_t i = 0; i < row_count; ++i) {
    result.Append(ApplyFloatArithmetic(op, AsFloat(left, i), AsFloat(right, i)));
  }
  for (std::size_t i = 0; i < row_count; ++i) {
    if (!(IsValidAt(left, i) && IsValidAt(right, i))) {
      result.SetNull(i, true);
    }
  }
  return result;
}

template <typename T>
bool ApplyComparison(BinaryOperator op, const T& lhs, const T& rhs) {
  switch (op) {
    case BinaryOperator::kEqual:
      return lhs == rhs;
    case BinaryOperator::kNotEqual:
      return lhs != rhs;
    case BinaryOperator::kLessThan:
      return lhs < rhs;
    case BinaryOperator::kLessThanOrEqual:
      return lhs <= rhs;
    case BinaryOperator::kGreaterThan:
      return lhs > rhs;
    case BinaryOperator::kGreaterThanOrEqual:
      return lhs >= rhs;
    default:
      return false;
  }
}

bool CompareRow(BinaryOperator op, const EvaluationResult& left, const EvaluationResult& right,
                std::size_t i) {
  if (std::holds_alternative<gistdb::storage::VarcharColumn>(left)) {
    return ApplyComparison(op, std::get<gistdb::storage::VarcharColumn>(left).GetValue(i),
                           std::get<gistdb::storage::VarcharColumn>(right).GetValue(i));
  }
  if (IsFloatResult(left) || IsFloatResult(right)) {
    return ApplyComparison(op, AsFloat(left, i), AsFloat(right, i));
  }
  return ApplyComparison(op, AsInt(left, i), AsInt(right, i));
}

EvaluationResult EvaluateComparison(BinaryOperator op, const EvaluationResult& left,
                                    const EvaluationResult& right, std::size_t row_count) {
  BooleanResult result{gistdb::storage::ValidityBitmap(row_count),
                       gistdb::storage::ValidityBitmap(row_count)};
  for (std::size_t i = 0; i < row_count; ++i) {
    result.values.SetValid(i, CompareRow(op, left, right, i));
  }
  for (std::size_t i = 0; i < row_count; ++i) {
    result.validity.SetValid(i, IsValidAt(left, i) && IsValidAt(right, i));
  }
  return result;
}

EvaluationResult EvaluateLogical(BinaryOperator op, const BooleanResult& left,
                                 const BooleanResult& right, std::size_t row_count) {
  BooleanResult result{gistdb::storage::ValidityBitmap(row_count),
                       gistdb::storage::ValidityBitmap(row_count)};
  for (std::size_t i = 0; i < row_count; ++i) {
    bool value = op == BinaryOperator::kAnd ? (left.values.IsValid(i) && right.values.IsValid(i))
                                            : (left.values.IsValid(i) || right.values.IsValid(i));
    result.values.SetValid(i, value);
  }
  for (std::size_t i = 0; i < row_count; ++i) {
    result.validity.SetValid(i, left.validity.IsValid(i) && right.validity.IsValid(i));
  }
  return result;
}

bool IsArithmeticOperator(BinaryOperator op) {
  return op == BinaryOperator::kAdd || op == BinaryOperator::kSubtract ||
         op == BinaryOperator::kMultiply || op == BinaryOperator::kDivide;
}

bool IsLogicalOperator(BinaryOperator op) {
  return op == BinaryOperator::kAnd || op == BinaryOperator::kOr;
}

EvaluationResult EvaluateBinaryOp(const BinaryOpNode& node, const DataChunk& chunk) {
  EvaluationResult left = EvaluateExpr(*node.left, chunk);
  EvaluationResult right = EvaluateExpr(*node.right, chunk);
  std::size_t row_count = chunk.RowCount();

  if (IsArithmeticOperator(node.op)) {
    if (node.arithmetic_result_type == ExpressionType::kFloat) {
      return EvaluateArithmeticFloat(node.op, left, right, row_count);
    }
    return EvaluateArithmeticInt(node.op, left, right, row_count);
  }
  if (IsLogicalOperator(node.op)) {
    return EvaluateLogical(node.op, std::get<BooleanResult>(left), std::get<BooleanResult>(right),
                           row_count);
  }
  return EvaluateComparison(node.op, left, right, row_count);
}

EvaluationResult EvaluateUnaryOp(const UnaryOpNode& node, const DataChunk& chunk) {
  EvaluationResult operand = EvaluateExpr(*node.operand, chunk);
  const auto& bool_operand = std::get<BooleanResult>(operand);
  std::size_t row_count = chunk.RowCount();

  BooleanResult result{gistdb::storage::ValidityBitmap(row_count),
                       gistdb::storage::ValidityBitmap(row_count)};
  for (std::size_t i = 0; i < row_count; ++i) {
    result.values.SetValid(i, !bool_operand.values.IsValid(i));
  }
  for (std::size_t i = 0; i < row_count; ++i) {
    result.validity.SetValid(i, bool_operand.validity.IsValid(i));
  }
  return result;
}

EvaluationResult EvaluateExpr(const BoundExpression& expr, const DataChunk& chunk) {
  return std::visit(
      [&chunk](const auto& node) -> EvaluationResult {
        using NodeType = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeType, BoundColumnRef>) {
          return EvaluateColumnRef(node, chunk);
        } else if constexpr (std::is_same_v<NodeType, ConstNode>) {
          return EvaluateConst(node, chunk.RowCount());
        } else if constexpr (std::is_same_v<NodeType, BinaryOpNode>) {
          return EvaluateBinaryOp(node, chunk);
        } else {  // UnaryOpNode
          return EvaluateUnaryOp(node, chunk);
        }
      },
      expr.node);
}

}  // namespace

EvaluationResult ExpressionEvaluator::Evaluate(const BoundExpression& expr,
                                               const DataChunk& chunk) {
  return EvaluateExpr(expr, chunk);
}

}  // namespace gistdb::execution