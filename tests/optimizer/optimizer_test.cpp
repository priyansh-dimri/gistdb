#include "gistdb/optimizer/optimizer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/catalog/column_def.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"
#include "gistdb/storage/column_footer_entry.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/page_range.hpp"
#include "gistdb/storage/row_group_footer_entry.hpp"
#include "gistdb/storage/varchar_column.hpp"
#include "gistdb/types.hpp"

namespace gistdb::optimizer {
namespace {

using gistdb::binder::AggregateCall;
using gistdb::binder::AggregateFunctionKind;
using gistdb::binder::MakeLogicalAggregate;
using gistdb::binder::MakeLogicalFilter;
using gistdb::binder::MakeLogicalJoin;
using gistdb::binder::MakeLogicalProjection;
using gistdb::binder::MakeLogicalScan;
using gistdb::catalog::Catalog;
using gistdb::catalog::ColumnDef;
using gistdb::execution::BinaryOperator;
using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
using gistdb::execution::ExpressionType;
using gistdb::execution::MakeArithmeticOp;
using gistdb::execution::MakeBooleanOp;
using gistdb::execution::MakeColumnRef;
using gistdb::execution::MakeIntConst;
using gistdb::storage::BufferPoolManager;
using gistdb::storage::ColumnFooterEntry;
using gistdb::storage::FixedWidthColumn;
using gistdb::storage::FixedWidthColumnFooterEntry;
using gistdb::storage::PageRange;
using gistdb::storage::RowGroupFooterEntry;
using gistdb::storage::VarcharColumn;
using gistdb::storage::VarcharColumnFooterEntry;
using gistdb::test_utils::ScopedTempFile;

std::unique_ptr<BoundExpression> ColRef(std::uint32_t table_id, std::uint32_t ordinal,
                                        gistdb::TypeId type = gistdb::TypeId::kInteger) {
  return MakeColumnRef(table_id, ordinal, type);
}

std::vector<std::uint8_t> MakeIntPage(const std::vector<std::int32_t>& values) {
  std::vector<std::uint8_t> page(gistdb::kPageSizeBytes, 0);
  std::memcpy(page.data(), values.data(), values.size() * sizeof(std::int32_t));
  return page;
}

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

std::uint32_t CreateSingleColumnTable(Catalog& catalog, const std::string& table_name,
                                      const std::vector<std::int32_t>& values) {
  std::uint32_t table_id =
      catalog.CreateTable(table_name, {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});
  auto& disk = catalog.GetDiskManager();

  std::uint32_t data_page = disk.AllocatePages(1);
  disk.WritePages(data_page, MakeIntPage(values));

  std::vector<std::uint8_t> validity_page(gistdb::kPageSizeBytes, 0);
  for (std::size_t i = 0; i < values.size(); ++i) {
    validity_page[i / 8] |= static_cast<std::uint8_t>(1U << (i % 8));  // all valid
  }
  std::uint32_t validity_page_id = disk.AllocatePages(1);
  disk.WritePages(validity_page_id, validity_page);

  FixedWidthColumn<std::int32_t> real_column;
  for (auto v : values) {
    real_column.Append(v);
  }
  auto entry =
      FixedWidthColumnFooterEntry<std::int32_t>::Build(real_column, PageRange{data_page, 1});

  RowGroupFooterEntry row_group(table_id, static_cast<std::uint32_t>(values.size()),
                                PageRange{validity_page_id, 1},
                                std::vector<ColumnFooterEntry>{ColumnFooterEntry{entry}});
  catalog.AddRowGroup(table_name, row_group);
  return table_id;
}

// A (VARCHAR category, INTEGER value) table, one row group, all non-null.
std::uint32_t CreateCategoryValueTable(Catalog& catalog, const std::string& table_name,
                                       const std::vector<std::string>& categories,
                                       const std::vector<std::int32_t>& values) {
  std::uint32_t table_id =
      catalog.CreateTable(table_name, {ColumnDef{"category", gistdb::TypeId::kVarchar, 0},
                                       ColumnDef{"value", gistdb::TypeId::kInteger, 1}});
  auto& disk = catalog.GetDiskManager();

  VarcharColumn real_category;
  for (const auto& c : categories) {
    real_category.Append(c);
  }

  std::vector<std::uint8_t> data_buffer_page(gistdb::kPageSizeBytes, 0);
  std::memcpy(data_buffer_page.data(), real_category.DataBuffer(), real_category.DataBufferSize());
  std::uint32_t category_data_page = disk.AllocatePages(1);
  disk.WritePages(category_data_page, data_buffer_page);

  std::vector<std::uint8_t> offsets_page(gistdb::kPageSizeBytes, 0);
  std::memcpy(offsets_page.data(), real_category.Offsets(),
              real_category.NumOffsets() * sizeof(std::int32_t));
  std::uint32_t offsets_page_id = disk.AllocatePages(1);
  disk.WritePages(offsets_page_id, offsets_page);

  auto category_entry = VarcharColumnFooterEntry::Build(
      real_category, PageRange{offsets_page_id, 1}, PageRange{category_data_page, 1});

  std::uint32_t value_data_page = disk.AllocatePages(1);
  disk.WritePages(value_data_page, MakeIntPage(values));
  FixedWidthColumn<std::int32_t> real_value;
  for (auto v : values) {
    real_value.Append(v);
  }
  auto value_entry =
      FixedWidthColumnFooterEntry<std::int32_t>::Build(real_value, PageRange{value_data_page, 1});

  const std::size_t n = values.size();
  const std::size_t bytes_per_column = (n + 7) / 8;
  std::vector<std::uint8_t> validity_page(gistdb::kPageSizeBytes, 0);
  for (std::size_t col = 0; col < 2; ++col) {
    for (std::size_t i = 0; i < n; ++i) {
      validity_page[(col * bytes_per_column) + (i / 8)] |= static_cast<std::uint8_t>(1U << (i % 8));
    }
  }
  std::uint32_t validity_page_id = disk.AllocatePages(1);
  disk.WritePages(validity_page_id, validity_page);

  RowGroupFooterEntry row_group(table_id, static_cast<std::uint32_t>(n),
                                PageRange{validity_page_id, 1},
                                std::vector<ColumnFooterEntry>{ColumnFooterEntry{category_entry},
                                                               ColumnFooterEntry{value_entry}});
  catalog.AddRowGroup(table_name, row_group);
  return table_id;
}

TEST(OptimizerTest, BareScanReturnsRealDataFromDisk) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateSingleColumnTable(catalog, "t", {10, 20, 30});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(0, table_id, {{"val", ExpressionType::kInteger}});
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(scan), std::move(select_exprs),
                                    {{"val", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RowCount(), 3U);
  const auto* col = std::get<const FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(col->GetValue(0), 10);
  EXPECT_EQ(col->GetValue(1), 20);
  EXPECT_EQ(col->GetValue(2), 30);
}

TEST(OptimizerTest, FilterOverScanFiltersRealRowsWithoutTriggeringZoneMapSkip) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateSingleColumnTable(catalog, "t", {5, 15, 25});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(0, table_id, {{"val", ExpressionType::kInteger}});
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(10));
  auto filter = MakeLogicalFilter(std::move(scan), std::move(predicate));
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(filter), std::move(select_exprs),
                                    {{"val", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 3U);
  EXPECT_FALSE(result->IsRowSelected(0));  // 5 > 10 false
  EXPECT_TRUE(result->IsRowSelected(1));   // 15 > 10 true
  EXPECT_TRUE(result->IsRowSelected(2));   // 25 > 10 true
}

TEST(OptimizerTest, ZoneMapSkipEliminatesRowGroupThatCannotMatch) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateSingleColumnTable(catalog, "t", {1, 2, 3});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(0, table_id, {{"val", ExpressionType::kInteger}});
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(1000));
  auto filter = MakeLogicalFilter(std::move(scan), std::move(predicate));
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(filter), std::move(select_exprs),
                                    {{"val", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  EXPECT_FALSE(op->GetNext().has_value());
}

TEST(OptimizerTest, NoGroupByAggregateProducesRealCountResult) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateSingleColumnTable(catalog, "t", {1, 2, 3, 4});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(0, table_id, {{"val", ExpressionType::kInteger}});
  std::vector<BoundColumnRef> group_by;
  std::vector<AggregateCall> aggregates = {
      AggregateCall{AggregateFunctionKind::kCountStar, std::nullopt}};
  auto root = MakeLogicalAggregate(std::move(scan), group_by, aggregates,
                                   {{"count", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RowCount(), 1U);
  const auto* col = std::get<const FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(col->GetValue(0), 4);
}

TEST(OptimizerTest, GroupByAggregateProducesOneRowPerDistinctGroup) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateCategoryValueTable(catalog, "t", {"a", "b", "a"}, {10, 20, 30});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(
      0, table_id, {{"category", ExpressionType::kVarchar}, {"value", ExpressionType::kInteger}});
  std::vector<BoundColumnRef> group_by = {BoundColumnRef{0, 0, gistdb::TypeId::kVarchar}};
  std::vector<AggregateCall> aggregates = {
      AggregateCall{AggregateFunctionKind::kSum, BoundColumnRef{0, 1, gistdb::TypeId::kInteger}}};
  auto root = MakeLogicalAggregate(
      std::move(scan), group_by, aggregates,
      {{"category", ExpressionType::kVarchar}, {"sum_value", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 2U);  // two distinct categories: "a", "b"
}

TEST(OptimizerTest, ProjectionAppliesArithmeticExpressionOverRealScanData) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateSingleColumnTable(catalog, "t", {1, 2, 3});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(0, table_id, {{"val", ExpressionType::kInteger}});
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(MakeArithmeticOp(BinaryOperator::kMultiply, ColRef(0, 0), MakeIntConst(10),
                                          ExpressionType::kInteger));
  auto root = MakeLogicalProjection(std::move(scan), std::move(select_exprs),
                                    {{"val_times_10", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  const auto* col = std::get<const FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(col->GetValue(0), 10);
  EXPECT_EQ(col->GetValue(1), 20);
  EXPECT_EQ(col->GetValue(2), 30);
}

TEST(OptimizerTest, JoinOfTwoBaseTablesProducesCorrectMatches) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t left_id = CreateSingleColumnTable(catalog, "left_t", {1, 2, 3});
  std::uint32_t right_id = CreateSingleColumnTable(catalog, "right_t", {2, 3, 4});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto left_scan = MakeLogicalScan(0, left_id, {{"val", ExpressionType::kInteger}});
  auto right_scan = MakeLogicalScan(1, right_id, {{"val", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{0, 0, gistdb::TypeId::kInteger},
       BoundColumnRef{1, 0, gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  select_exprs.push_back(ColRef(1, 0));
  auto root = MakeLogicalProjection(
      std::move(join), std::move(select_exprs),
      {{"left_val", ExpressionType::kInteger}, {"right_val", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 2U);  // matches: (2,2) and (3,3)
}

TEST(OptimizerTest, SelfJoinDistinguishesBindingIdsFromTheSamePhysicalTable) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  std::uint32_t table_id = CreateSingleColumnTable(catalog, "t", {1, 2, 3});
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto left_scan = MakeLogicalScan(0, table_id, {{"val", ExpressionType::kInteger}});
  auto right_scan =
      MakeLogicalScan(1, table_id, {{"val", ExpressionType::kInteger}});  // same physical table
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{0, 0, gistdb::TypeId::kInteger},
       BoundColumnRef{1, 0, gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto root = MakeLogicalProjection(std::move(join), std::move(select_exprs),
                                    {{"val", ExpressionType::kInteger}});

  auto op = Optimizer::Optimize(std::move(root), catalog, bpm);
  auto result = op->GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 3U);  // each row matches only itself: (1,1),(2,2),(3,3)
}

TEST(OptimizerTest, UnknownPhysicalTableIdThrows) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());

  auto scan = MakeLogicalScan(0, /*physical_table_id=*/9999, {{"val", ExpressionType::kInteger}});
  EXPECT_THROW((void)Optimizer::Optimize(std::move(scan), catalog, bpm), std::runtime_error);
}

}  // namespace
}  // namespace gistdb::optimizer