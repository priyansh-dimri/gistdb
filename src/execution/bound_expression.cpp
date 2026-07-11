#include "gistdb/execution/bound_expression.hpp"

#include <type_traits>
#include <utility>

namespace gistdb::execution {

namespace {

ExpressionType ResultTypeForBinaryOp(const BinaryOpNode& node) {
  switch (node.op) {
    case BinaryOperator::kAdd:
    case BinaryOperator::kSubtract:
    case BinaryOperator::kMultiply:
    case BinaryOperator::kDivide:
      return node.arithmetic_result_type;
    default:
      return ExpressionType::kBoolean;
  }
}

}  // namespace

ExpressionType ToExpressionType(gistdb::TypeId type) {
  switch (type) {
    case gistdb::TypeId::kInteger:
      return ExpressionType::kInteger;
    case gistdb::TypeId::kFloat:
      return ExpressionType::kFloat;
    case gistdb::TypeId::kVarchar:
      return ExpressionType::kVarchar;
  }
  return ExpressionType::kVarchar;
}

ExpressionType BoundExpression::ResultType() const {
  return std::visit(
      [](const auto& n) -> ExpressionType {
        using NodeType = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<NodeType, BoundColumnRef>) {
          return ToExpressionType(n.type);
        } else if constexpr (std::is_same_v<NodeType, ConstNode>) {
          return std::visit(
              [](const auto& value) -> ExpressionType {
                using ValueType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<ValueType, std::int32_t>) {
                  return ExpressionType::kInteger;
                } else if constexpr (std::is_same_v<ValueType, float>) {
                  return ExpressionType::kFloat;
                } else {
                  return ExpressionType::kVarchar;
                }
              },
              n.value);
        } else if constexpr (std::is_same_v<NodeType, BinaryOpNode>) {
          return ResultTypeForBinaryOp(n);
        } else {  // UnaryOpNode
          return ExpressionType::kBoolean;
        }
      },
      node);
}

std::unique_ptr<BoundExpression> MakeColumnRef(std::uint32_t table_id, std::uint32_t ordinal,
                                               gistdb::TypeId type) {
  return std::make_unique<BoundExpression>(BoundExpression{BoundColumnRef{
      .table_id = table_id,
      .ordinal = ordinal,
      .type = type,
  }});
}

std::unique_ptr<BoundExpression> MakeIntConst(std::int32_t value) {
  return std::make_unique<BoundExpression>(BoundExpression{ConstNode{value}});
}

std::unique_ptr<BoundExpression> MakeFloatConst(float value) {
  return std::make_unique<BoundExpression>(BoundExpression{ConstNode{value}});
}

std::unique_ptr<BoundExpression> MakeStringConst(std::string value) {
  return std::make_unique<BoundExpression>(BoundExpression{ConstNode{std::move(value)}});
}

std::unique_ptr<BoundExpression> MakeArithmeticOp(BinaryOperator op,
                                                  std::unique_ptr<BoundExpression> left,
                                                  std::unique_ptr<BoundExpression> right,
                                                  ExpressionType result_type) {
  return std::make_unique<BoundExpression>(BoundExpression{BinaryOpNode{
      .op = op,
      .left = std::move(left),
      .right = std::move(right),
      .arithmetic_result_type = result_type,
  }});
}

std::unique_ptr<BoundExpression> MakeBooleanOp(BinaryOperator op,
                                               std::unique_ptr<BoundExpression> left,
                                               std::unique_ptr<BoundExpression> right) {
  return std::make_unique<BoundExpression>(BoundExpression{BinaryOpNode{
      .op = op,
      .left = std::move(left),
      .right = std::move(right),
      .arithmetic_result_type = ExpressionType::kInteger,
  }});
}

std::unique_ptr<BoundExpression> MakeNot(std::unique_ptr<BoundExpression> operand) {
  return std::make_unique<BoundExpression>(BoundExpression{UnaryOpNode{
      .op = UnaryOperator::kNot,
      .operand = std::move(operand),
  }});
}

}  // namespace gistdb::execution