#include "gistdb/catalog/catalog.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "../test_utils/scoped_temp_file.hpp"

namespace gistdb::catalog {
namespace {

using gistdb::test_utils::ScopedTempFile;

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

std::vector<ColumnDef> SampleColumns() {
  return {
      ColumnDef{
          .name = "id",
          .type = gistdb::TypeId::kInteger,
          .ordinal = 0,
      },
      ColumnDef{
          .name = "name",
          .type = gistdb::TypeId::kVarchar,
          .ordinal = 1,
      },
  };
}

TEST(CatalogTest, CreateNewBootstrapsEmptyCatalog) {
  ScopedTempFile temp;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp));
  EXPECT_EQ(catalog.GetTable("users"), nullptr);
}

TEST(CatalogTest, CreateTableThenGetTableReturnsIt) {
  ScopedTempFile temp;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp));

  std::uint32_t table_id = catalog.CreateTable("users", SampleColumns());

  const TableObject* table = catalog.GetTable("users");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->TableId(), table_id);
  EXPECT_EQ(table->TableName(), "users");
  EXPECT_EQ(table->NumColumns(), 2U);
}

TEST(CatalogTest, TableIdsAreSequentialStartingAtZero) {
  ScopedTempFile temp;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp));

  std::uint32_t first = catalog.CreateTable("users", SampleColumns());
  std::uint32_t second = catalog.CreateTable("orders", SampleColumns());

  EXPECT_EQ(first, 0U);
  EXPECT_EQ(second, 1U);
}

TEST(CatalogTest, CreateTableRejectsDuplicateName) {
  ScopedTempFile temp;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp));
  catalog.CreateTable("users", SampleColumns());
  EXPECT_THROW(catalog.CreateTable("users", SampleColumns()), std::invalid_argument);
}

TEST(CatalogTest, AddRowGroupAttachesToNamedTableAndUpdatesRowCount) {
  ScopedTempFile temp;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = catalog.CreateTable("users", SampleColumns());

  gistdb::storage::RowGroupFooterEntry row_group(table_id, 10240,
                                                 gistdb::storage::PageRange{
                                                     .start_page_id = 1,
                                                     .page_count = 1,
                                                 },
                                                 {});
  catalog.AddRowGroup("users", row_group);

  const TableObject* table = catalog.GetTable("users");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->RowGroups().size(), 1U);
  EXPECT_EQ(table->TotalRowCount(), 10240U);
}

TEST(CatalogTest, AddRowGroupRejectsUnknownTable) {
  ScopedTempFile temp;
  Catalog catalog = Catalog::CreateNew(FreshPath(temp));
  gistdb::storage::RowGroupFooterEntry row_group(0, 100,
                                                 gistdb::storage::PageRange{
                                                     .start_page_id = 1,
                                                     .page_count = 1,
                                                 },
                                                 {});
  EXPECT_THROW(catalog.AddRowGroup("nonexistent", row_group), std::invalid_argument);
}

TEST(CatalogTest, OpenReconstructsSchemaAcrossCloseAndReopen) {
  ScopedTempFile temp;
  std::filesystem::path path = FreshPath(temp);

  {
    Catalog catalog = Catalog::CreateNew(path);
    catalog.CreateTable("users", SampleColumns());
  }

  Catalog reopened = Catalog::Open(path);
  const TableObject* table = reopened.GetTable("users");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->TableId(), 0U);
  EXPECT_EQ(table->NumColumns(), 2U);
  EXPECT_EQ(table->Column(1).name, "name");
}

TEST(CatalogTest, OpenReconstructsRowGroupsPartitionedByTable) {
  ScopedTempFile temp;
  std::filesystem::path path = FreshPath(temp);

  {
    Catalog catalog = Catalog::CreateNew(path);
    std::uint32_t users_id = catalog.CreateTable("users", SampleColumns());
    std::uint32_t orders_id = catalog.CreateTable("orders", SampleColumns());

    catalog.AddRowGroup("users", gistdb::storage::RowGroupFooterEntry(users_id, 10240,
                                                                      gistdb::storage::PageRange{
                                                                          .start_page_id = 1,
                                                                          .page_count = 1,
                                                                      },
                                                                      {}));

    catalog.AddRowGroup("orders", gistdb::storage::RowGroupFooterEntry(orders_id, 500,
                                                                       gistdb::storage::PageRange{
                                                                           .start_page_id = 2,
                                                                           .page_count = 1,
                                                                       },
                                                                       {}));
  }

  Catalog reopened = Catalog::Open(path);

  const TableObject* users = reopened.GetTable("users");
  const TableObject* orders = reopened.GetTable("orders");
  ASSERT_NE(users, nullptr);
  ASSERT_NE(orders, nullptr);

  EXPECT_EQ(users->RowGroups().size(), 1U);
  EXPECT_EQ(users->TotalRowCount(), 10240U);
  EXPECT_EQ(orders->RowGroups().size(), 1U);
  EXPECT_EQ(orders->TotalRowCount(), 500U);
}

TEST(CatalogTest, CreateTableAfterReopenContinuesTableIdSequence) {
  ScopedTempFile temp;
  std::filesystem::path path = FreshPath(temp);

  {
    Catalog catalog = Catalog::CreateNew(path);
    catalog.CreateTable("users", SampleColumns());
  }

  Catalog reopened = Catalog::Open(path);
  std::uint32_t next_id = reopened.CreateTable("orders", SampleColumns());
  EXPECT_EQ(next_id, 1U);
}

}  // namespace
}  // namespace gistdb::catalog