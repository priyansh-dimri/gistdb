#include "gistdb/binder/ast.hpp"

#include <utility>

namespace gistdb::binder {

std::unique_ptr<RawExpression> MakeColumnRef(std::optional<std::string> qualifier,
                                             std::string column_name) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = ColumnRefNode{
      .table_qualifier = std::move(qualifier),
      .column_name = std::move(column_name),
  };
  return expr;
}

std::unique_ptr<RawExpression> MakeNullConst() {
  auto expr = std::make_unique<RawExpression>();
  expr->node = ConstNode{NullLiteral{}};
  return expr;
}

std::unique_ptr<RawExpression> MakeIntConst(std::int64_t value) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = ConstNode{value};
  return expr;
}

std::unique_ptr<RawExpression> MakeFloatConst(double value) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = ConstNode{value};
  return expr;
}

std::unique_ptr<RawExpression> MakeStringConst(std::string value) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = ConstNode{std::move(value)};
  return expr;
}

std::unique_ptr<RawExpression> MakeBinaryOp(BinaryOperator op, std::unique_ptr<RawExpression> left,
                                            std::unique_ptr<RawExpression> right) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = BinaryOpNode{
      .op = op,
      .left = std::move(left),
      .right = std::move(right),
  };
  return expr;
}

std::unique_ptr<RawExpression> MakeUnaryOp(UnaryOperator op,
                                           std::unique_ptr<RawExpression> operand) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = UnaryOpNode{.op = op, .operand = std::move(operand)};
  return expr;
}

std::unique_ptr<RawExpression> MakeFunctionCall(std::string name,
                                                std::vector<std::unique_ptr<RawExpression>> args,
                                                bool is_star_arg) {
  auto expr = std::make_unique<RawExpression>();
  expr->node = FunctionCallNode{
      .name = std::move(name), .args = std::move(args), .is_star_arg = is_star_arg};
  return expr;
}

std::unique_ptr<TableRefNode> MakeBaseTableRef(std::string table_name,
                                               std::optional<std::string> alias) {
  auto ref = std::make_unique<TableRefNode>();
  ref->node = BaseTableRefNode{.table_name = std::move(table_name), .alias = std::move(alias)};
  return ref;
}

std::unique_ptr<TableRefNode> MakeJoinRef(std::unique_ptr<TableRefNode> left,
                                          std::unique_ptr<TableRefNode> right,
                                          std::unique_ptr<RawExpression> on_condition) {
  auto ref = std::make_unique<TableRefNode>();
  ref->node = JoinRefNode{
      .left = std::move(left), .right = std::move(right), .on_condition = std::move(on_condition)};
  return ref;
}

}  // namespace gistdb::binder