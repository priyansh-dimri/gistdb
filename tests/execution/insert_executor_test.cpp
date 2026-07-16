#include "gistdb/execution/insert_executor.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/catalog/column_def.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/seq_scan_operator.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"
#include "gistdb/types.hpp"

namespace gistdb::execution {
namespace {

using gistdb::catalog::Catalog;
using gistdb::catalog::ColumnDef;
using gistdb::storage::BufferPoolManager;
using gistdb::storage::FixedWidthColumn;
using gistdb::storage::VarcharColumn;
using gistdb::test_utils::ScopedTempFile;

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

std::vector<std::unique_ptr<BoundExpression>> IntRow(std::int32_t value) {
  std::vector<std::unique_ptr<BoundExpression>> row;
  row.push_back(MakeIntConst(value));
  return row;
}

TEST(InsertExecutorTest, NoFlushOccursBeforeFinishForRowCountBelowThreshold) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  executor.InsertRow(IntRow(1));
  executor.InsertRow(IntRow(2));
  executor.InsertRow(IntRow(3));

  EXPECT_TRUE(catalog.GetTable("t")->RowGroups().empty());
}

TEST(InsertExecutorTest, FinishFlushesRemainingRowsAsAShortFinalRowGroup) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  executor.InsertRow(IntRow(1));
  executor.InsertRow(IntRow(2));
  executor.InsertRow(IntRow(3));
  executor.Finish();

  const auto* table = catalog.GetTable("t");
  ASSERT_EQ(table->RowGroups().size(), 1U);
  EXPECT_EQ(table->RowGroups()[0].RowCount(), 3U);
  EXPECT_EQ(table->TotalRowCount(), 3U);
}

TEST(InsertExecutorTest, InsertedIntegerValuesRoundTripCorrectlyThroughSeqScan) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  executor.InsertRow(IntRow(10));
  executor.InsertRow(IntRow(20));
  executor.InsertRow(IntRow(30));
  executor.Finish();

  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  SeqScanOperator scan(*catalog.GetTable("t"), {0}, bpm);
  auto result = scan.GetNext();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RowCount(), 3U);

  const auto* col = std::get<const FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(col->GetValue(0), 10);
  EXPECT_EQ(col->GetValue(1), 20);
  EXPECT_EQ(col->GetValue(2), 30);
}

TEST(InsertExecutorTest, InsertedVarcharValuesRoundTripCorrectly) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"name", gistdb::TypeId::kVarchar, 0}});

  InsertExecutor executor(catalog, table_id);
  {
    std::vector<std::unique_ptr<BoundExpression>> row;
    row.push_back(MakeStringConst("alice"));
    executor.InsertRow(row);
  }
  {
    std::vector<std::unique_ptr<BoundExpression>> row;
    row.push_back(MakeStringConst("bob"));
    executor.InsertRow(row);
  }
  executor.Finish();

  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  SeqScanOperator scan(*catalog.GetTable("t"), {0}, bpm);
  auto result = scan.GetNext();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RowCount(), 2U);

  const auto* col = std::get<const VarcharColumn*>(result->Column(0));
  EXPECT_EQ(col->GetValue(0), "alice");
  EXPECT_EQ(col->GetValue(1), "bob");
}

TEST(InsertExecutorTest, MultipleColumnsStayRowAligned) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"id", gistdb::TypeId::kInteger, 0},
                                ColumnDef{"name", gistdb::TypeId::kVarchar, 1}});

  InsertExecutor executor(catalog, table_id);
  {
    std::vector<std::unique_ptr<BoundExpression>> row;
    row.push_back(MakeIntConst(1));
    row.push_back(MakeStringConst("alice"));
    executor.InsertRow(row);
  }
  {
    std::vector<std::unique_ptr<BoundExpression>> row;
    row.push_back(MakeIntConst(2));
    row.push_back(MakeStringConst("bob"));
    executor.InsertRow(row);
  }
  executor.Finish();

  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  SeqScanOperator scan(*catalog.GetTable("t"), {0, 1}, bpm);
  auto result = scan.GetNext();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RowCount(), 2U);

  const auto* id_col = std::get<const FixedWidthColumn<std::int32_t>*>(result->Column(0));
  const auto* name_col = std::get<const VarcharColumn*>(result->Column(1));
  EXPECT_EQ(id_col->GetValue(0), 1);
  EXPECT_EQ(name_col->GetValue(0), "alice");
  EXPECT_EQ(id_col->GetValue(1), 2);
  EXPECT_EQ(name_col->GetValue(1), "bob");
}

TEST(InsertExecutorTest, ReachingRowGroupSizeTriggersAutomaticFlushWithoutFinish) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  for (std::uint32_t i = 0; i < gistdb::kRowGroupSize; ++i) {
    executor.InsertRow(IntRow(static_cast<std::int32_t>(i)));
  }

  const auto* table = catalog.GetTable("t");
  ASSERT_EQ(table->RowGroups().size(), 1U);
  EXPECT_EQ(table->RowGroups()[0].RowCount(), gistdb::kRowGroupSize);
}

TEST(InsertExecutorTest, ExceedingOneRowGroupProducesTwoRowGroupsAfterFinish) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  for (std::uint32_t i = 0; i < gistdb::kRowGroupSize + 5; ++i) {
    executor.InsertRow(IntRow(static_cast<std::int32_t>(i)));
  }
  executor.Finish();

  const auto* table = catalog.GetTable("t");
  ASSERT_EQ(table->RowGroups().size(), 2U);
  EXPECT_EQ(table->RowGroups()[0].RowCount(), gistdb::kRowGroupSize);
  EXPECT_EQ(table->RowGroups()[1].RowCount(), 5U);
  EXPECT_EQ(table->TotalRowCount(), gistdb::kRowGroupSize + 5U);
}

TEST(InsertExecutorTest, ThrowsWhenInsertValueIsNotABoundConstant) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  std::vector<std::unique_ptr<BoundExpression>> row;
  row.push_back(MakeColumnRef(0, 0, gistdb::TypeId::kInteger));   
  EXPECT_THROW(executor.InsertRow(row), std::runtime_error);
}

TEST(InsertExecutorTest, ThrowsWhenBoundValueTypeDoesNotMatchColumnType) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id =
      catalog.CreateTable("t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});

  InsertExecutor executor(catalog, table_id);
  std::vector<std::unique_ptr<BoundExpression>> row;
  row.push_back(MakeStringConst("not an int"));
  EXPECT_THROW(executor.InsertRow(row), std::runtime_error);
}

}  // namespace
}  // namespace gistdb::execution