#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::binder {

using gistdb::execution::BinaryOperator;
using gistdb::execution::UnaryOperator;

struct RawExpression;

struct ColumnRefNode {
  std::optional<std::string> table_qualifier;
  std::string column_name;
};

struct NullLiteral {};

struct ConstNode {
  std::variant<NullLiteral, std::int64_t, double, std::string> value;
};

struct BinaryOpNode {
  BinaryOperator op;
  std::unique_ptr<RawExpression> left;
  std::unique_ptr<RawExpression> right;
};

struct UnaryOpNode {
  UnaryOperator op;
  std::unique_ptr<RawExpression> operand;
};

struct FunctionCallNode {
  std::string name;
  std::vector<std::unique_ptr<RawExpression>> args;
  bool is_star_arg = false;
};

struct RawExpression {
  std::variant<ColumnRefNode, ConstNode, BinaryOpNode, UnaryOpNode, FunctionCallNode> node;
};

struct TableRefNode;

struct BaseTableRefNode {
  std::string table_name;
  std::optional<std::string> alias;
};

struct JoinRefNode {
  std::unique_ptr<TableRefNode> left;
  std::unique_ptr<TableRefNode> right;
  std::unique_ptr<RawExpression> on_condition;
};

struct TableRefNode {
  std::variant<BaseTableRefNode, JoinRefNode> node;
};

struct SelectItem {
  bool is_wildcard = false;
  std::unique_ptr<RawExpression> expression;  // null if is_wildcard
};

struct SelectNode {
  std::vector<SelectItem> select_list;
  std::vector<std::unique_ptr<TableRefNode>> from_tables;
  std::unique_ptr<RawExpression> where_clause;  // null if absent
  std::vector<std::unique_ptr<RawExpression>> group_by;
  std::unique_ptr<RawExpression> having_clause;  // null if absent
};

struct InsertNode {
  std::string table_name;
  std::vector<std::string> columns;
  std::vector<std::vector<std::unique_ptr<RawExpression>>> value_rows;
};

struct RawColumnDef {
  std::string name;
  std::string raw_type_name;
};

struct CreateTableNode {
  std::string table_name;
  std::vector<RawColumnDef> columns;
};

[[nodiscard]] std::unique_ptr<RawExpression> MakeColumnRef(std::optional<std::string> qualifier,
                                                           std::string column_name);
[[nodiscard]] std::unique_ptr<RawExpression> MakeNullConst();
[[nodiscard]] std::unique_ptr<RawExpression> MakeIntConst(std::int64_t value);
[[nodiscard]] std::unique_ptr<RawExpression> MakeFloatConst(double value);
[[nodiscard]] std::unique_ptr<RawExpression> MakeStringConst(std::string value);
[[nodiscard]] std::unique_ptr<RawExpression> MakeBinaryOp(BinaryOperator op,
                                                          std::unique_ptr<RawExpression> left,
                                                          std::unique_ptr<RawExpression> right);
[[nodiscard]] std::unique_ptr<RawExpression> MakeUnaryOp(UnaryOperator op,
                                                         std::unique_ptr<RawExpression> operand);
[[nodiscard]] std::unique_ptr<RawExpression> MakeFunctionCall(
    std::string name, std::vector<std::unique_ptr<RawExpression>> args, bool is_star_arg = false);

[[nodiscard]] std::unique_ptr<TableRefNode> MakeBaseTableRef(std::string table_name,
                                                             std::optional<std::string> alias);
[[nodiscard]] std::unique_ptr<TableRefNode> MakeJoinRef(
    std::unique_ptr<TableRefNode> left, std::unique_ptr<TableRefNode> right,
    std::unique_ptr<RawExpression> on_condition);

}  // namespace gistdb::binder