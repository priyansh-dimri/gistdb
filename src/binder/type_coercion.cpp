#include "gistdb/binder/type_coercion.hpp"

#include <limits>
#include <type_traits>
#include <variant>

namespace gistdb::binder {

using gistdb::execution::BinaryOperator;
using gistdb::execution::UnaryOperator;

bool IsComparisonOperator(BinaryOperator op) {
  switch (op) {
    case BinaryOperator::kEqual:
    case BinaryOperator::kNotEqual:
    case BinaryOperator::kLessThan:
    case BinaryOperator::kLessThanOrEqual:
    case BinaryOperator::kGreaterThan:
    case BinaryOperator::kGreaterThanOrEqual:
      return true;
    default:
      return false;
  }
}

bool IsLogicalOperator(BinaryOperator op) {
  return op == BinaryOperator::kAnd || op == BinaryOperator::kOr;
}

bool RequiresFloatPromotion(ExpressionType left, ExpressionType right) {
  return (left == ExpressionType::kInteger && right == ExpressionType::kFloat) ||
         (left == ExpressionType::kFloat && right == ExpressionType::kInteger);
}

ExpressionType ResultTypeForBinaryOp(BinaryOperator op, ExpressionType left, ExpressionType right) {
  if (IsLogicalOperator(op)) {
    if (left != ExpressionType::kBoolean || right != ExpressionType::kBoolean) {
      throw TypeCoercionException("AND/OR require boolean operands");
    }
    return ExpressionType::kBoolean;
  }

  const bool either_is_varchar =
      left == ExpressionType::kVarchar || right == ExpressionType::kVarchar;
  const bool both_are_varchar =
      left == ExpressionType::kVarchar && right == ExpressionType::kVarchar;
  if (either_is_varchar && !both_are_varchar) {
    throw TypeCoercionException("Cannot mix VARCHAR with a numeric type");
  }

  if (IsComparisonOperator(op)) {
    return ExpressionType::kBoolean;
  }

  if (both_are_varchar) {
    throw TypeCoercionException("VARCHAR does not support arithmetic operators");
  }
  if (left == ExpressionType::kFloat || right == ExpressionType::kFloat) {
    return ExpressionType::kFloat;
  }
  return ExpressionType::kInteger;
}

ExpressionType ResultTypeForUnaryOp(UnaryOperator /*op*/, ExpressionType operand) {
  if (operand != ExpressionType::kBoolean) {
    throw TypeCoercionException("NOT requires a boolean operand");
  }
  return ExpressionType::kBoolean;
}

std::unique_ptr<gistdb::execution::BoundExpression> BindConst(const ConstNode& raw_const) {
  return std::visit(
      [](const auto& value) -> std::unique_ptr<gistdb::execution::BoundExpression> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, NullLiteral>) {
          throw TypeCoercionException(
              "NULL literal binding is not yet supported (see this module's header comment)");
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          if (value < std::numeric_limits<std::int32_t>::min() ||
              value > std::numeric_limits<std::int32_t>::max()) {
            throw TypeCoercionException("Integer literal out of range for INTEGER (int32)");
          }
          return gistdb::execution::MakeIntConst(static_cast<std::int32_t>(value));
        } else if constexpr (std::is_same_v<T, double>) {
          return gistdb::execution::MakeFloatConst(static_cast<float>(value));
        } else {
          return gistdb::execution::MakeStringConst(value);
        }
      },
      raw_const.value);
}

}  // namespace gistdb::binder