#include "gistdb/execution/seq_scan_operator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/catalog/column_def.hpp"
#include "gistdb/catalog/table_object.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"
#include "gistdb/storage/column_footer_entry.hpp"
#include "gistdb/storage/disk_manager.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/row_group_footer_entry.hpp"

namespace gistdb::execution {
namespace {

using gistdb::catalog::ColumnDef;
using gistdb::catalog::TableObject;
using gistdb::storage::BufferPoolManager;
using gistdb::storage::ColumnFooterEntry;
using gistdb::storage::DiskManager;
using gistdb::storage::FixedWidthColumn;
using gistdb::storage::FixedWidthColumnFooterEntry;
using gistdb::storage::PageRange;
using gistdb::storage::RowGroupFooterEntry;
using gistdb::test_utils::ScopedTempFile;

struct Fixture {
  DiskManager disk;
  TableObject table;
};

std::vector<std::uint8_t> MakeIntPage(std::vector<std::int32_t> values) {
  std::vector<std::uint8_t> page(gistdb::kPageSizeBytes, 0);
  std::memcpy(page.data(), values.data(), values.size() * sizeof(std::int32_t));
  return page;
}

std::vector<std::uint8_t> MakeValidityPage(std::vector<bool> valid_flags) {
  std::vector<std::uint8_t> page(gistdb::kPageSizeBytes, 0);
  for (std::size_t i = 0; i < valid_flags.size(); ++i) {
    if (valid_flags[i]) {
      page[i / 8] |= static_cast<std::uint8_t>(1U << (i % 8));
    }
  }
  return page;
}

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

Fixture BuildSingleRowGroupTable(const std::filesystem::path& path) {
  DiskManager disk = DiskManager::CreateNew(path);

  std::uint32_t data_page = disk.AllocatePages(1);
  disk.WritePages(data_page, MakeIntPage({10, 0, 30}));

  std::uint32_t validity_page = disk.AllocatePages(1);
  disk.WritePages(validity_page, MakeValidityPage({true, false, true}));

  FixedWidthColumn<std::int32_t> real_column;
  real_column.Append(10);
  real_column.AppendNull();
  real_column.Append(30);
  auto entry =
      FixedWidthColumnFooterEntry<std::int32_t>::Build(real_column, PageRange{data_page, 1});

  RowGroupFooterEntry row_group(/*table_id=*/1, /*row_count=*/3, PageRange{validity_page, 1},
                                std::vector<ColumnFooterEntry>{ColumnFooterEntry{entry}});

  TableObject table(1, "t", {ColumnDef{"val", gistdb::TypeId::kInteger, 0}});
  table.AddRowGroup(row_group);

  return Fixture{std::move(disk), std::move(table)};
}

TEST(SeqScanOperatorTest, BasicScanReturnsAllRowsInOneVector) {
  ScopedTempFile temp;
  Fixture fixture = BuildSingleRowGroupTable(FreshPath(temp));
  BufferPoolManager bpm(8, &fixture.disk);

  SeqScanOperator scan(fixture.table, {0}, bpm);
  auto result = scan.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 3U);

  const auto* col = std::get<const FixedWidthColumn<std::int32_t>*>(result->Column(0));
  EXPECT_EQ(col->GetValue(0), 10);
  EXPECT_TRUE(col->IsNull(1));
  EXPECT_EQ(col->GetValue(2), 30);
}

TEST(SeqScanOperatorTest, ReturnsNulloptAfterRowGroupExhausted) {
  ScopedTempFile temp;
  Fixture fixture = BuildSingleRowGroupTable(FreshPath(temp));
  BufferPoolManager bpm(8, &fixture.disk);

  SeqScanOperator scan(fixture.table, {0}, bpm);
  ASSERT_TRUE(scan.GetNext().has_value());
  EXPECT_FALSE(scan.GetNext().has_value());
}

TEST(SeqScanOperatorTest, ZoneMapSkipEliminatesRowGroupThatCannotMatch) {
  ScopedTempFile temp;
  Fixture fixture = BuildSingleRowGroupTable(FreshPath(temp));
  BufferPoolManager bpm(8, &fixture.disk);

  ZoneMapSkipCondition condition{0, BinaryOperator::kGreaterThan, std::int32_t{100}};
  SeqScanOperator scan(fixture.table, {0}, bpm, condition);

  EXPECT_FALSE(scan.GetNext().has_value());
}

TEST(SeqScanOperatorTest, ZoneMapDoesNotSkipRowGroupThatCouldMatch) {
  ScopedTempFile temp;
  Fixture fixture = BuildSingleRowGroupTable(FreshPath(temp));
  BufferPoolManager bpm(8, &fixture.disk);

  ZoneMapSkipCondition condition{0, BinaryOperator::kGreaterThan, std::int32_t{5}};
  SeqScanOperator scan(fixture.table, {0}, bpm, condition);

  auto result = scan.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 3U);
}

}  // namespace
}  // namespace gistdb::execution