#include "gistdb/catalog/table_object.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace gistdb::catalog {
namespace {

std::vector<ColumnDef> SampleColumns() {
  return {
      ColumnDef{.name = "id", .type = gistdb::TypeId::kInteger, .ordinal = 0},
      ColumnDef{.name = "name", .type = gistdb::TypeId::kVarchar, .ordinal = 1},
      ColumnDef{.name = "score", .type = gistdb::TypeId::kFloat, .ordinal = 2},
  };
}

TEST(TableObjectTest, ConstructionStoresIdNameAndColumns) {
  TableObject table(7, "users", SampleColumns());
  EXPECT_EQ(table.TableId(), 7U);
  EXPECT_EQ(table.TableName(), "users");
  EXPECT_EQ(table.NumColumns(), 3U);
}

TEST(TableObjectTest, ColumnAccessByOrdinal) {
  TableObject table(1, "users", SampleColumns());
  EXPECT_EQ(table.Column(0).name, "id");
  EXPECT_EQ(table.Column(1).name, "name");
  EXPECT_EQ(table.Column(2).type, gistdb::TypeId::kFloat);
}

TEST(TableObjectTest, FindColumnByNameReturnsCorrectOrdinal) {
  TableObject table(1, "users", SampleColumns());
  const ColumnDef* found = table.FindColumn("score");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->ordinal, 2U);
}

TEST(TableObjectTest, FindColumnReturnsNullptrForUnknownName) {
  TableObject table(1, "users", SampleColumns());
  EXPECT_EQ(table.FindColumn("nonexistent"), nullptr);
}

TEST(TableObjectTest, StartsWithNoRowGroupsAndZeroRowCount) {
  TableObject table(1, "users", SampleColumns());
  EXPECT_EQ(table.RowGroups().size(), 0U);
  EXPECT_EQ(table.TotalRowCount(), 0U);
}

TEST(TableObjectTest, AddRowGroupAccumulatesRowCount) {
  TableObject table(1, "users", SampleColumns());

  gistdb::storage::RowGroupFooterEntry rg1(1, 10240, gistdb::storage::PageRange{0, 1}, {});
  gistdb::storage::RowGroupFooterEntry rg2(1, 780, gistdb::storage::PageRange{1, 1}, {});

  table.AddRowGroup(rg1);
  table.AddRowGroup(rg2);

  EXPECT_EQ(table.RowGroups().size(), 2U);
  EXPECT_EQ(table.TotalRowCount(), 10240U + 780U);
}

TEST(TableObjectTest, AddRowGroupRejectsMismatchedTableId) {
  TableObject table(1, "users", SampleColumns());
  gistdb::storage::RowGroupFooterEntry wrong_table_rg(2, 100, gistdb::storage::PageRange{0, 1}, {});
  EXPECT_THROW(table.AddRowGroup(wrong_table_rg), std::invalid_argument);
}

}  // namespace
}  // namespace gistdb::catalog