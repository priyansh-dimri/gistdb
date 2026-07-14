#include "gistdb/binder/parser.hpp"

#include <pg_query.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "protobuf/pg_query.pb-c.h"

namespace gistdb::binder {

namespace {

using gistdb::execution::BinaryOperator;
using gistdb::execution::UnaryOperator;

class ScopedProtobufParseResult {  // NOLINT
 public:
  explicit ScopedProtobufParseResult(const std::string& sql) {
    result_ = pg_query_parse_protobuf(sql.c_str());
  }
  ~ScopedProtobufParseResult() { pg_query_free_protobuf_parse_result(result_); }
  ScopedProtobufParseResult(const ScopedProtobufParseResult&) = delete;
  ScopedProtobufParseResult& operator=(const ScopedProtobufParseResult&) = delete;

  [[nodiscard]] bool HasError() const { return result_.error != nullptr; }
  [[nodiscard]] std::string ErrorMessage() const { return result_.error->message; }
  [[nodiscard]] const PgQueryProtobuf& Tree() const { return result_.parse_tree; }

 private:
  PgQueryProtobufParseResult result_{};
};

class ScopedParseResult {  // NOLINT
 public:
  explicit ScopedParseResult(const PgQueryProtobuf& tree) {
    unpacked_ = pg_query__parse_result__unpack(nullptr, tree.len,  // NOLINT
                                               reinterpret_cast<const std::uint8_t*>(tree.data));
  }
  ~ScopedParseResult() {
    if (unpacked_ != nullptr) {
      pg_query__parse_result__free_unpacked(unpacked_, nullptr);
    }
  }
  ScopedParseResult(const ScopedParseResult&) = delete;
  ScopedParseResult& operator=(const ScopedParseResult&) = delete;

  [[nodiscard]] PgQuery__ParseResult* Get() const { return unpacked_; }

 private:
  PgQuery__ParseResult* unpacked_ = nullptr;
};

[[nodiscard]] BinaryOperator ToOperator(const std::string& op_name) {
  if (op_name == "=") {
    return BinaryOperator::kEqual;
  }
  if (op_name == "<>" || op_name == "!=") {
    return BinaryOperator::kNotEqual;
  }
  if (op_name == "<") {
    return BinaryOperator::kLessThan;
  }
  if (op_name == "<=") {
    return BinaryOperator::kLessThanOrEqual;
  }
  if (op_name == ">") {
    return BinaryOperator::kGreaterThan;
  }
  if (op_name == ">=") {
    return BinaryOperator::kGreaterThanOrEqual;
  }
  if (op_name == "+") {
    return BinaryOperator::kAdd;
  }
  if (op_name == "-") {
    return BinaryOperator::kSubtract;
  }
  if (op_name == "*") {
    return BinaryOperator::kMultiply;
  }
  if (op_name == "/") {
    return BinaryOperator::kDivide;
  }
  throw ParseException("Unsupported operator: " + op_name);
}

[[nodiscard]] std::unique_ptr<RawExpression> ConvertExpr(const PgQuery__Node& node);
[[nodiscard]] std::unique_ptr<RawExpression> ConvertAConst(const PgQuery__AConst& value) {
  if (value.isnull) {
    return MakeNullConst();
  }
  switch (value.val_case) {
    case PG_QUERY__A__CONST__VAL_IVAL:
      return MakeIntConst(value.ival->ival);
    case PG_QUERY__A__CONST__VAL_FVAL:
      return MakeFloatConst(std::stod(value.fval->fval));
    case PG_QUERY__A__CONST__VAL_SVAL:
      return MakeStringConst(value.sval->sval);
    default:
      throw ParseException("Unsupported constant shape in A_Const");
  }
}

[[nodiscard]] std::unique_ptr<RawExpression> ConvertColumnRef(const PgQuery__ColumnRef& ref) {
  if (ref.n_fields == 1 && ref.fields[0]->node_case == PG_QUERY__NODE__NODE_STRING) {
    return MakeColumnRef(std::nullopt, ref.fields[0]->string->sval);
  }
  if (ref.n_fields == 2 && ref.fields[0]->node_case == PG_QUERY__NODE__NODE_STRING &&
      ref.fields[1]->node_case == PG_QUERY__NODE__NODE_STRING) {
    return MakeColumnRef(std::string(ref.fields[0]->string->sval), ref.fields[1]->string->sval);
  }
  throw ParseException("Unsupported column reference shape (e.g. qualified '*' is not supported)");
}

[[nodiscard]] std::unique_ptr<RawExpression> ConvertAExpr(const PgQuery__AExpr& expr) {
  if (expr.n_name != 1) {
    throw ParseException("Unsupported operator name shape");
  }
  BinaryOperator op = ToOperator(expr.name[0]->string->sval);
  return MakeBinaryOp(op, ConvertExpr(*expr.lexpr), ConvertExpr(*expr.rexpr));
}

[[nodiscard]] std::unique_ptr<RawExpression> ConvertBoolExpr(const PgQuery__BoolExpr& expr) {
  if (expr.boolop == PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR) {
    if (expr.n_args != 1) {
      throw ParseException("NOT expects exactly one operand");
    }
    return MakeUnaryOp(UnaryOperator::kNot, ConvertExpr(*expr.args[0]));
  }
  const bool is_and = expr.boolop == PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR;
  if (expr.n_args < 2) {
    throw ParseException("AND/OR expects at least two operands");
  }

  std::unique_ptr<RawExpression> result = ConvertExpr(*expr.args[0]);
  for (std::size_t i = 1; i < expr.n_args; ++i) {
    result = MakeBinaryOp(is_and ? BinaryOperator::kAnd : BinaryOperator::kOr, std::move(result),
                          ConvertExpr(*expr.args[i]));
  }
  return result;
}

[[nodiscard]] std::unique_ptr<RawExpression> ConvertFuncCall(const PgQuery__FuncCall& call) {
  if (call.n_funcname != 1) {
    throw ParseException("Qualified function names are not supported");
  }
  std::vector<std::unique_ptr<RawExpression>> args;
  for (std::size_t i = 0; i < call.n_args; ++i) {
    {
      args.push_back(ConvertExpr(*call.args[i]));
    }
  }
  return MakeFunctionCall(call.funcname[0]->string->sval, std::move(args), call.agg_star);
}

std::unique_ptr<RawExpression> ConvertExpr(const PgQuery__Node& node) {
  switch (node.node_case) {
    case PG_QUERY__NODE__NODE_A_CONST:
      return ConvertAConst(*node.a_const);
    case PG_QUERY__NODE__NODE_COLUMN_REF:
      return ConvertColumnRef(*node.column_ref);
    case PG_QUERY__NODE__NODE_A_EXPR:
      return ConvertAExpr(*node.a_expr);
    case PG_QUERY__NODE__NODE_BOOL_EXPR:
      return ConvertBoolExpr(*node.bool_expr);
    case PG_QUERY__NODE__NODE_FUNC_CALL:
      return ConvertFuncCall(*node.func_call);
    default:
      throw ParseException("Unsupported expression node type");
  }
}

[[nodiscard]] std::unique_ptr<TableRefNode> ConvertFromItem(const PgQuery__Node& node);

[[nodiscard]] std::unique_ptr<TableRefNode> ConvertRangeVar(const PgQuery__RangeVar& rv) {
  std::optional<std::string> alias;
  if (rv.alias != nullptr) {
    alias = rv.alias->aliasname;
  }
  return MakeBaseTableRef(rv.relname, std::move(alias));
}

[[nodiscard]] std::unique_ptr<TableRefNode> ConvertJoinExpr(const PgQuery__JoinExpr& join) {
  if (join.jointype != PG_QUERY__JOIN_TYPE__JOIN_INNER) {
    throw ParseException(
        "Only INNER JOIN is supported -- no locked decision addresses outer-join NULL-padding "
        "semantics, and Hash Join's design has no mechanism for it");
  }
  auto left = ConvertFromItem(*join.larg);
  auto right = ConvertFromItem(*join.rarg);
  std::unique_ptr<RawExpression> on_condition;
  if (join.quals != nullptr) {
    on_condition = ConvertExpr(*join.quals);
  }
  return MakeJoinRef(std::move(left), std::move(right), std::move(on_condition));
}

std::unique_ptr<TableRefNode> ConvertFromItem(const PgQuery__Node& node) {
  switch (node.node_case) {
    case PG_QUERY__NODE__NODE_RANGE_VAR:
      return ConvertRangeVar(*node.range_var);
    case PG_QUERY__NODE__NODE_JOIN_EXPR:
      return ConvertJoinExpr(*node.join_expr);
    default:
      throw ParseException("Unsupported FROM-clause item");
  }
}

[[nodiscard]] bool IsStarColumnRef(const PgQuery__Node& node) {
  return node.node_case == PG_QUERY__NODE__NODE_COLUMN_REF && node.column_ref->n_fields == 1 &&
         node.column_ref->fields[0]->node_case == PG_QUERY__NODE__NODE_A_STAR;
}

[[nodiscard]] SelectItem ConvertTargetItem(const PgQuery__Node& node) {
  if (node.node_case != PG_QUERY__NODE__NODE_RES_TARGET) {
    throw ParseException("Expected ResTarget in select list");
  }
  const PgQuery__ResTarget& target = *node.res_target;
  if (IsStarColumnRef(*target.val)) {
    return SelectItem{.is_wildcard = true, .expression = nullptr};
  }
  return SelectItem{.is_wildcard = false, .expression = ConvertExpr(*target.val)};
}

[[nodiscard]] std::unique_ptr<SelectNode> ConvertSelectStmt(const PgQuery__SelectStmt& stmt) {
  auto select = std::make_unique<SelectNode>();
  for (std::size_t i = 0; i < stmt.n_target_list; ++i) {
    select->select_list.push_back(ConvertTargetItem(*stmt.target_list[i]));
  }
  for (std::size_t i = 0; i < stmt.n_from_clause; ++i) {
    select->from_tables.push_back(ConvertFromItem(*stmt.from_clause[i]));
  }
  if (stmt.where_clause != nullptr) {
    select->where_clause = ConvertExpr(*stmt.where_clause);
  }
  for (std::size_t i = 0; i < stmt.n_group_clause; ++i) {
    select->group_by.push_back(ConvertExpr(*stmt.group_clause[i]));
  }
  if (stmt.having_clause != nullptr) {
    select->having_clause = ConvertExpr(*stmt.having_clause);
  }
  select->has_distinct = stmt.n_distinct_clause > 0;
  select->has_order_by = stmt.n_sort_clause > 0;
  select->has_limit = stmt.limit_count != nullptr || stmt.limit_offset != nullptr;
  select->has_with_clause = stmt.with_clause != nullptr;
  select->has_set_operation = stmt.op != PG_QUERY__SET_OPERATION__SETOP_NONE;

  return select;
}

[[nodiscard]] std::unique_ptr<InsertNode> ConvertInsertStmt(const PgQuery__InsertStmt& stmt) {
  auto insert = std::make_unique<InsertNode>();
  insert->table_name = stmt.relation->relname;
  for (std::size_t i = 0; i < stmt.n_cols; ++i) {
    if (stmt.cols[i]->node_case != PG_QUERY__NODE__NODE_RES_TARGET) {
      throw ParseException("Unsupported INSERT column-list entry");
    }
    insert->columns.emplace_back(stmt.cols[i]->res_target->name);
  }
  if (stmt.select_stmt == nullptr ||
      stmt.select_stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT) {
    throw ParseException("INSERT must supply a VALUES list");
  }
  const PgQuery__SelectStmt& values = *stmt.select_stmt->select_stmt;
  for (std::size_t i = 0; i < values.n_values_lists; ++i) {
    const PgQuery__Node& row = *values.values_lists[i];
    if (row.node_case != PG_QUERY__NODE__NODE_LIST) {
      throw ParseException("Malformed VALUES row");
    }
    std::vector<std::unique_ptr<RawExpression>> row_values;
    for (std::size_t j = 0; j < row.list->n_items; ++j) {
      row_values.push_back(ConvertExpr(*row.list->items[j]));  // NOLINT
    }
    insert->value_rows.push_back(std::move(row_values));
  }
  return insert;
}

[[nodiscard]] std::unique_ptr<CreateTableNode> ConvertCreateStmt(const PgQuery__CreateStmt& stmt) {
  auto create = std::make_unique<CreateTableNode>();
  create->table_name = stmt.relation->relname;
  for (std::size_t i = 0; i < stmt.n_table_elts; ++i) {
    const PgQuery__Node& elt = *stmt.table_elts[i];
    if (elt.node_case != PG_QUERY__NODE__NODE_COLUMN_DEF) {
      throw ParseException("Only plain column definitions are supported in CREATE TABLE");
    }
    const PgQuery__ColumnDef& col = *elt.column_def;
    if (col.type_name == nullptr || col.type_name->n_names == 0) {
      throw ParseException("Column definition is missing a type name");
    }
    std::string raw_type = col.type_name->names[col.type_name->n_names - 1]->string->sval;
    create->columns.push_back(RawColumnDef{.name = col.colname, .raw_type_name = raw_type});
  }
  return create;
}

}  // namespace

ParsedStatement Parser::ParseSingleStatement(const std::string& sql) {
  ScopedProtobufParseResult parsed(sql);
  if (parsed.HasError()) {
    throw ParseException(parsed.ErrorMessage());
  }
  ScopedParseResult unpacked(parsed.Tree());
  PgQuery__ParseResult* tree = unpacked.Get();
  if (tree == nullptr) {
    throw ParseException("Failed to unpack parse tree");
  }
  if (tree->n_stmts != 1) {
    throw ParseException("Expected exactly one SQL statement");
  }
  const PgQuery__Node& stmt = *tree->stmts[0]->stmt;
  switch (stmt.node_case) {
    case PG_QUERY__NODE__NODE_SELECT_STMT:
      return ConvertSelectStmt(*stmt.select_stmt);
    case PG_QUERY__NODE__NODE_INSERT_STMT:
      return ConvertInsertStmt(*stmt.insert_stmt);
    case PG_QUERY__NODE__NODE_CREATE_STMT:
      return ConvertCreateStmt(*stmt.create_stmt);
    default:
      throw ParseException("Unsupported statement type");
  }
}

}  // namespace gistdb::binder