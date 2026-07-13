#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "gistdb/binder/ast.hpp"
#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::binder {

using gistdb::execution::ExpressionType;

class TypeCoercionException : public std::runtime_error {
 public:
  explicit TypeCoercionException(const std::string& message) : std::runtime_error(message) {}
};

[[nodiscard]] bool IsComparisonOperator(gistdb::execution::BinaryOperator op);

[[nodiscard]] bool IsLogicalOperator(gistdb::execution::BinaryOperator op);

[[nodiscard]] bool RequiresFloatPromotion(ExpressionType left, ExpressionType right);

[[nodiscard]] ExpressionType ResultTypeForBinaryOp(gistdb::execution::BinaryOperator op,
                                                   ExpressionType left, ExpressionType right);

[[nodiscard]] ExpressionType ResultTypeForUnaryOp(gistdb::execution::UnaryOperator op,
                                                  ExpressionType operand);

[[nodiscard]] std::unique_ptr<gistdb::execution::BoundExpression> BindConst(
    const ConstNode& raw_const);

}  // namespace gistdb::binder