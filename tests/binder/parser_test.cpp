#include "gistdb/binder/parser.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "gistdb/binder/ast.hpp"

namespace gistdb::binder {
namespace {

TEST(ParserTest, ParsesSimpleSelectWithColumnAndTable) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT id FROM users");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);

  ASSERT_EQ(select.select_list.size(), 1U);
  EXPECT_FALSE(select.select_list[0].is_wildcard);
  const auto& col_ref = std::get<ColumnRefNode>(select.select_list[0].expression->node);
  EXPECT_EQ(col_ref.column_name, "id");
  EXPECT_FALSE(col_ref.table_qualifier.has_value());

  ASSERT_EQ(select.from_tables.size(), 1U);
  const auto& table = std::get<BaseTableRefNode>(select.from_tables[0]->node);
  EXPECT_EQ(table.table_name, "users");
}

TEST(ParserTest, ParsesSelectStarAsWildcardItem) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT * FROM users");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);

  ASSERT_EQ(select.select_list.size(), 1U);
  EXPECT_TRUE(select.select_list[0].is_wildcard);
  EXPECT_EQ(select.select_list[0].expression, nullptr);
}

TEST(ParserTest, ParsesQualifiedColumnAndTableAlias) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT u.name FROM users u");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);

  const auto& col_ref = std::get<ColumnRefNode>(select.select_list[0].expression->node);
  ASSERT_TRUE(col_ref.table_qualifier.has_value());
  EXPECT_EQ(*col_ref.table_qualifier, "u");
  EXPECT_EQ(col_ref.column_name, "name");

  const auto& table = std::get<BaseTableRefNode>(select.from_tables[0]->node);
  EXPECT_EQ(table.table_name, "users");
  ASSERT_TRUE(table.alias.has_value());
  EXPECT_EQ(*table.alias, "u");
}

TEST(ParserTest, ParsesWhereClauseAsComparison) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT id FROM users WHERE age > 18");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);

  ASSERT_NE(select.where_clause, nullptr);
  const auto& cmp = std::get<BinaryOpNode>(select.where_clause->node);
  EXPECT_EQ(cmp.op, BinaryOperator::kGreaterThan);
  EXPECT_EQ(std::get<ColumnRefNode>(cmp.left->node).column_name, "age");
  EXPECT_EQ(std::get<std::int64_t>(std::get<ConstNode>(cmp.right->node).value), 18);
}

TEST(ParserTest, ParsesInnerJoinWithOnCondition) {
  ParsedStatement result =
      Parser::ParseSingleStatement("SELECT * FROM users u JOIN orders o ON u.id = o.user_id");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);

  ASSERT_EQ(select.from_tables.size(), 1U);
  const auto& join = std::get<JoinRefNode>(select.from_tables[0]->node);
  EXPECT_EQ(std::get<BaseTableRefNode>(join.left->node).table_name, "users");
  EXPECT_EQ(std::get<BaseTableRefNode>(join.right->node).table_name, "orders");
  ASSERT_NE(join.on_condition, nullptr);
  EXPECT_EQ(std::get<BinaryOpNode>(join.on_condition->node).op, BinaryOperator::kEqual);
}

TEST(ParserTest, ParsesCountStarAsStarArgFunctionCall) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT COUNT(*) FROM users");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);

  const auto& call = std::get<FunctionCallNode>(select.select_list[0].expression->node);
  EXPECT_EQ(call.name, "count");
  EXPECT_TRUE(call.is_star_arg);
  EXPECT_TRUE(call.args.empty());
}

TEST(ParserTest, ParsesInsertWithExplicitColumnsAndValues) {
  ParsedStatement result =
      Parser::ParseSingleStatement("INSERT INTO users (id, name) VALUES (1, 'Alice')");
  auto& insert = *std::get<std::unique_ptr<InsertNode>>(result);

  EXPECT_EQ(insert.table_name, "users");
  ASSERT_EQ(insert.columns.size(), 2U);
  EXPECT_EQ(insert.columns[0], "id");
  EXPECT_EQ(insert.columns[1], "name");

  ASSERT_EQ(insert.value_rows.size(), 1U);
  ASSERT_EQ(insert.value_rows[0].size(), 2U);
  EXPECT_EQ(std::get<std::int64_t>(std::get<ConstNode>(insert.value_rows[0][0]->node).value), 1);
  EXPECT_EQ(std::get<std::string>(std::get<ConstNode>(insert.value_rows[0][1]->node).value),
            "Alice");
}

TEST(ParserTest, ParsesCreateTableWithColumnDefinitions) {
  ParsedStatement result =
      Parser::ParseSingleStatement("CREATE TABLE users (id int4, name varchar)");
  auto& create = *std::get<std::unique_ptr<CreateTableNode>>(result);

  EXPECT_EQ(create.table_name, "users");
  ASSERT_EQ(create.columns.size(), 2U);
  EXPECT_EQ(create.columns[0].name, "id");
  EXPECT_EQ(create.columns[0].raw_type_name, "int4");
  EXPECT_EQ(create.columns[1].name, "name");
  EXPECT_EQ(create.columns[1].raw_type_name, "varchar");
}

TEST(ParserTest, ThrowsOnSyntaxError) {
  EXPECT_THROW((void)Parser::ParseSingleStatement("SELECT * FROM"), ParseException);
}

TEST(ParserTest, ThrowsOnMoreThanOneStatement) {
  EXPECT_THROW((void)Parser::ParseSingleStatement("SELECT 1; SELECT 2;"), ParseException);
}

TEST(ParserTest, ThrowsOnUnsupportedStatementType) {
  EXPECT_THROW((void)Parser::ParseSingleStatement("DELETE FROM users"), ParseException);
}

TEST(ParserTest, ThrowsOnQualifiedWildcardInsteadOfSegfaulting) {
  EXPECT_THROW((void)Parser::ParseSingleStatement("SELECT u.* FROM users u"), ParseException);
}

TEST(ParserTest, SelectStarStillWorksAfterColumnRefGuard) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT * FROM users");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  ASSERT_EQ(select.select_list.size(), 1U);
  EXPECT_TRUE(select.select_list[0].is_wildcard);
}

TEST(ParserTest, QualifiedColumnRefStillWorksAfterGuard) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT u.id FROM users u");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  const auto& col_ref = std::get<ColumnRefNode>(select.select_list[0].expression->node);
  ASSERT_TRUE(col_ref.table_qualifier.has_value());
  EXPECT_EQ(*col_ref.table_qualifier, "u");
  EXPECT_EQ(col_ref.column_name, "id");
}

TEST(ParserTest, PlainSelectHasNoRejectedClausesSet) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT id FROM users");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  EXPECT_FALSE(select.has_distinct);
  EXPECT_FALSE(select.has_order_by);
  EXPECT_FALSE(select.has_limit);
  EXPECT_FALSE(select.has_with_clause);
  EXPECT_FALSE(select.has_set_operation);
}

TEST(ParserTest, DetectsDistinctClause) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT DISTINCT id FROM users");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  EXPECT_TRUE(select.has_distinct);
}

TEST(ParserTest, DetectsOrderByClause) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT id FROM users ORDER BY id");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  EXPECT_TRUE(select.has_order_by);
}

TEST(ParserTest, DetectsLimitClause) {
  ParsedStatement result = Parser::ParseSingleStatement("SELECT id FROM users LIMIT 5");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  EXPECT_TRUE(select.has_limit);
}

TEST(ParserTest, DetectsSetOperation) {
  ParsedStatement result =
      Parser::ParseSingleStatement("SELECT id FROM users UNION SELECT id FROM orders");
  auto& select = *std::get<std::unique_ptr<SelectNode>>(result);
  EXPECT_TRUE(select.has_set_operation);
}

}  // namespace
}  // namespace gistdb::binder