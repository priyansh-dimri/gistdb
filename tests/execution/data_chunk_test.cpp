#include "gistdb/execution/data_chunk.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <variant>

#include "gistdb/constants.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {
namespace {

TEST(DataChunkTest, ConstructionStoresRowCount) {
  DataChunk chunk(5);
  EXPECT_EQ(chunk.RowCount(), 5U);
}

TEST(DataChunkTest, ConstructionRejectsRowCountAboveVectorSize) {
  EXPECT_THROW(DataChunk(kVectorSize + 1), std::invalid_argument);
}

TEST(DataChunkTest, ZeroRowCountIsValid) {
  DataChunk chunk(0);
  EXPECT_EQ(chunk.RowCount(), 0U);
  EXPECT_EQ(chunk.CountSelectedRows(), 0U);
}

TEST(DataChunkTest, AllRowsStartSelected) {
  DataChunk chunk(4);
  for (std::uint32_t i = 0; i < 4; ++i) {
    EXPECT_TRUE(chunk.IsRowSelected(i));
  }
  EXPECT_EQ(chunk.CountSelectedRows(), 4U);
}

TEST(DataChunkTest, SetRowSelectedTogglesIndividualRows) {
  DataChunk chunk(4);
  chunk.SetRowSelected(1, false);
  chunk.SetRowSelected(3, false);

  EXPECT_TRUE(chunk.IsRowSelected(0));
  EXPECT_FALSE(chunk.IsRowSelected(1));
  EXPECT_TRUE(chunk.IsRowSelected(2));
  EXPECT_FALSE(chunk.IsRowSelected(3));
  EXPECT_EQ(chunk.CountSelectedRows(), 2U);
}

TEST(DataChunkTest, AddColumnStoresNonOwningPointer) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(10);
  column.Append(20);
  column.Append(30);

  DataChunk chunk(3);
  chunk.AddColumn(&column);

  ASSERT_EQ(chunk.NumColumns(), 1U);
  const auto* stored =
      std::get<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(chunk.Column(0));
  EXPECT_EQ(stored->GetValue(0), 10);
  EXPECT_EQ(stored->GetValue(2), 30);
  EXPECT_EQ(stored, &column);
}

TEST(DataChunkTest, AddColumnRejectsMismatchedSize) {
  gistdb::storage::FixedWidthColumn<std::int32_t> column;
  column.Append(1);
  column.Append(2);

  DataChunk chunk(3);
  EXPECT_THROW(chunk.AddColumn(&column), std::invalid_argument);
}

TEST(DataChunkTest, SupportsMultipleColumnsOfDifferentTypes) {
  gistdb::storage::FixedWidthColumn<std::int32_t> ints;
  ints.Append(1);
  ints.Append(2);

  gistdb::storage::VarcharColumn strings;
  strings.Append("a");
  strings.Append("b");

  DataChunk chunk(2);
  chunk.AddColumn(&ints);
  chunk.AddColumn(&strings);

  EXPECT_EQ(chunk.NumColumns(), 2U);
  EXPECT_TRUE(std::holds_alternative<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(
      chunk.Column(0)));
  EXPECT_TRUE(std::holds_alternative<const gistdb::storage::VarcharColumn*>(chunk.Column(1)));
}

}  // namespace
}  // namespace gistdb::execution