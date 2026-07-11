#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include "gistdb/types.hpp"

namespace gistdb::execution {

enum class ExpressionType : std::uint8_t {
  kInteger,
  kFloat,
  kVarchar,
  kBoolean,
};

[[nodiscard]] ExpressionType ToExpressionType(gistdb::TypeId type);

enum class BinaryOperator : std::uint8_t {
  kAdd,
  kSubtract,
  kMultiply,
  kDivide,
  kEqual,
  kNotEqual,
  kLessThan,
  kLessThanOrEqual,
  kGreaterThan,
  kGreaterThanOrEqual,
  kAnd,
  kOr,
};

enum class UnaryOperator : std::uint8_t {
  kNot,
};

struct BoundExpression; 

struct BoundColumnRef {
  std::uint32_t table_id;
  std::uint32_t ordinal;
  gistdb::TypeId type;
};

struct ConstNode {
  std::variant<std::int32_t, float, std::string> value;
};

struct BinaryOpNode {
  BinaryOperator op;
  std::unique_ptr<BoundExpression> left;
  std::unique_ptr<BoundExpression> right;
  ExpressionType arithmetic_result_type = ExpressionType::kInteger;
};

struct UnaryOpNode {
  UnaryOperator op;
  std::unique_ptr<BoundExpression> operand;
};

struct BoundExpression {
  std::variant<BoundColumnRef, ConstNode, BinaryOpNode, UnaryOpNode> node;

  [[nodiscard]] ExpressionType ResultType() const;
};

[[nodiscard]] std::unique_ptr<BoundExpression> MakeColumnRef(std::uint32_t table_id,
                                                             std::uint32_t ordinal,
                                                             gistdb::TypeId type);
[[nodiscard]] std::unique_ptr<BoundExpression> MakeIntConst(std::int32_t value);
[[nodiscard]] std::unique_ptr<BoundExpression> MakeFloatConst(float value);
[[nodiscard]] std::unique_ptr<BoundExpression> MakeStringConst(std::string value);

[[nodiscard]] std::unique_ptr<BoundExpression> MakeArithmeticOp(
    BinaryOperator op, std::unique_ptr<BoundExpression> left,
    std::unique_ptr<BoundExpression> right, ExpressionType result_type);

[[nodiscard]] std::unique_ptr<BoundExpression> MakeBooleanOp(
    BinaryOperator op, std::unique_ptr<BoundExpression> left,
    std::unique_ptr<BoundExpression> right);

[[nodiscard]] std::unique_ptr<BoundExpression> MakeNot(std::unique_ptr<BoundExpression> operand);

}  // namespace gistdb::execution