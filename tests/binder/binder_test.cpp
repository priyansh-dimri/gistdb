#include "gistdb/binder/binder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/binder/parser.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/catalog/column_def.hpp"
#include "gistdb/catalog/table_object.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/types.hpp"

namespace gistdb::binder {
namespace {

using gistdb::catalog::Catalog;
using gistdb::catalog::ColumnDef;
using gistdb::test_utils::ScopedTempFile;

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

Catalog MakeTestCatalog(const std::filesystem::path& path) {
  Catalog catalog = Catalog::CreateNew(path);
  catalog.CreateTable("users", {ColumnDef{"id", gistdb::TypeId::kInteger, 0},
                                ColumnDef{"name", gistdb::TypeId::kVarchar, 1}});
  catalog.CreateTable("orders", {ColumnDef{"id", gistdb::TypeId::kInteger, 0},
                                 ColumnDef{"user_id", gistdb::TypeId::kInteger, 1}});
  return catalog;
}

TEST(BinderTest, CreateTableBindsAndCreatesRealTableInCatalog) {
  ScopedTempFile temp_file;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("CREATE TABLE users (id int4, name varchar)");
  BindResult result = Binder::Bind(stmt, catalog);

  const auto& created = std::get<TableCreated>(result);
  const gistdb::catalog::TableObject* table = catalog.GetTable("users");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->TableId(), created.table_id);
  EXPECT_EQ(table->NumColumns(), 2U);
}

TEST(BinderTest, SelectBareColumnsBindsToProjectionOverScan) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT id, name FROM users");
  BindResult result = Binder::Bind(stmt, catalog);

  auto& plan = *std::get<std::unique_ptr<LogicalPlanNode>>(result);
  const auto& projection = std::get<LogicalProjection>(plan.node);
  ASSERT_EQ(projection.output_columns.size(), 2U);

  const auto& scan = std::get<LogicalScan>(projection.input->node);
  EXPECT_EQ(scan.binding_id, 0U);
  EXPECT_EQ(scan.physical_table_id, catalog.GetTable("users")->TableId());
}

TEST(BinderTest, SelectStarExpandsToEveryColumn) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT * FROM users");
  BindResult result = Binder::Bind(stmt, catalog);

  auto& plan = *std::get<std::unique_ptr<LogicalPlanNode>>(result);
  const auto& projection = std::get<LogicalProjection>(plan.node);
  ASSERT_EQ(projection.output_columns.size(), 2U);
  EXPECT_EQ(projection.output_columns[0].display_name, "id");
  EXPECT_EQ(projection.output_columns[1].display_name, "name");
}

TEST(BinderTest, WhereClauseInsertsFilterBetweenScanAndProjection) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT id FROM users WHERE id = 1");
  BindResult result = Binder::Bind(stmt, catalog);

  auto& plan = *std::get<std::unique_ptr<LogicalPlanNode>>(result);
  const auto& projection = std::get<LogicalProjection>(plan.node);
  const auto& filter = std::get<LogicalFilter>(projection.input->node);
  ASSERT_NE(filter.predicate, nullptr);
  EXPECT_EQ(filter.predicate->ResultType(), gistdb::execution::ExpressionType::kBoolean);
}

TEST(BinderTest, InsertBindsRowsInOrdinalOrder) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("INSERT INTO users (id, name) VALUES (1, 'Alice')");
  BindResult result = Binder::Bind(stmt, catalog);

  const auto& insert = std::get<BoundInsert>(result);
  EXPECT_EQ(insert.table_id, catalog.GetTable("users")->TableId());
  ASSERT_EQ(insert.rows.size(), 1U);
  ASSERT_EQ(insert.rows[0].size(), 2U);

  const auto& id_value = std::get<gistdb::execution::ConstNode>(insert.rows[0][0]->node);
  EXPECT_EQ(std::get<std::int32_t>(id_value.value), 1);
  const auto& name_value = std::get<gistdb::execution::ConstNode>(insert.rows[0][1]->node);
  EXPECT_EQ(std::get<std::string>(name_value.value), "Alice");
}

TEST(BinderTest, InsertTypeMismatchThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("INSERT INTO users (id, name) VALUES ('not_a_number', 'Alice')");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, SelfJoinWithoutAliasesThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("SELECT * FROM users JOIN users ON users.id = users.id");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, SelfJoinWithAliasesBindsDistinctBindingIds) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("SELECT * FROM users u1 JOIN users u2 ON u1.id = u2.id");
  BindResult result = Binder::Bind(stmt, catalog);

  auto& plan = *std::get<std::unique_ptr<LogicalPlanNode>>(result);
  const auto& projection = std::get<LogicalProjection>(plan.node);
  const auto& join = std::get<LogicalJoin>(projection.input->node);

  const auto& left_scan = std::get<LogicalScan>(join.left->node);
  const auto& right_scan = std::get<LogicalScan>(join.right->node);
  EXPECT_NE(left_scan.binding_id, right_scan.binding_id);
  EXPECT_EQ(left_scan.physical_table_id, right_scan.physical_table_id);  // same real table

  ASSERT_EQ(join.equi_conditions.size(), 1U);
  EXPECT_EQ(join.equi_conditions[0].first.table_id, left_scan.binding_id);
  EXPECT_EQ(join.equi_conditions[0].second.table_id, right_scan.binding_id);
}

TEST(BinderTest, NonEquiJoinConditionThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("SELECT * FROM users u JOIN orders o ON u.id < o.user_id");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, CommaJoinWithConnectingWhereEqualityBinds) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("SELECT * FROM users, orders WHERE users.id = orders.user_id");
  BindResult result = Binder::Bind(stmt, catalog);

  auto& plan = *std::get<std::unique_ptr<LogicalPlanNode>>(result);
  const auto& projection = std::get<LogicalProjection>(plan.node);
  EXPECT_TRUE(std::holds_alternative<LogicalJoin>(projection.input->node));
}

TEST(BinderTest, CommaJoinWithoutConnectingConditionThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT * FROM users, orders");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, AggregateFunctionNestedInLargerExpressionThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT SUM(id) + 1 FROM users");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, NonAggregatedColumnNotInGroupByThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt =
      Parser::ParseSingleStatement("SELECT name, COUNT(*) FROM users GROUP BY id");
  try {
    (void)Binder::Bind(stmt, catalog);
    FAIL() << "Expected BindException";
  } catch (const BindException& e) {
    EXPECT_NE(std::string(e.what()).find("GROUP BY"), std::string::npos);
  }
}

TEST(BinderTest, InsertWithPartialColumnListIsCurrentlyBlocked) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("INSERT INTO users (id) VALUES (1)");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, UnknownTableInFromThrows) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT * FROM ghosts");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

TEST(BinderTest, LimitClauseIsRejected) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ParsedStatement stmt = Parser::ParseSingleStatement("SELECT id FROM users LIMIT 1");
  EXPECT_THROW((void)Binder::Bind(stmt, catalog), BindException);
}

}  // namespace
}  // namespace gistdb::binder