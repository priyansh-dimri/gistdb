#include "gistdb/storage/row_group_footer_entry.hpp"

#include <gtest/gtest.h>

#include "gistdb/constants.hpp"

namespace gistdb::storage {
namespace {

std::vector<ColumnFooterEntry> MakeTwoColumnFixtures() {
  FixedWidthColumn<std::int32_t> int_column;
  int_column.Append(1);
  int_column.Append(2);
  auto int_entry = FixedWidthColumnFooterEntry<std::int32_t>::Build(int_column, PageRange{0, 1});

  VarcharColumn varchar_column;
  varchar_column.Append("hi");
  auto varchar_entry =
      VarcharColumnFooterEntry::Build(varchar_column, PageRange{1, 1}, PageRange{2, 1});

  return {ColumnFooterEntry{int_entry}, ColumnFooterEntry{varchar_entry}};
}

TEST(RowGroupFooterEntryTest, ConstructionStoresAllFields) {
  RowGroupFooterEntry entry(3, kRowGroupSize, PageRange{10, 2}, MakeTwoColumnFixtures());

  EXPECT_EQ(entry.TableId(), 3u);
  EXPECT_EQ(entry.RowCount(), kRowGroupSize);
  EXPECT_EQ(entry.ValidityBitmapRegion(), (PageRange{10, 2}));
  EXPECT_EQ(entry.NumColumns(), 2u);
}

TEST(RowGroupFooterEntryTest, ColumnAccessReturnsCorrectVariantAlternative) {
  RowGroupFooterEntry entry(1, kRowGroupSize, PageRange{0, 1}, MakeTwoColumnFixtures());

  ASSERT_TRUE(std::holds_alternative<FixedWidthColumnFooterEntry<std::int32_t>>(entry.Column(0)));
  ASSERT_TRUE(std::holds_alternative<VarcharColumnFooterEntry>(entry.Column(1)));

  const auto& int_entry = std::get<FixedWidthColumnFooterEntry<std::int32_t>>(entry.Column(0));
  EXPECT_EQ(int_entry.Zone().Min(), 1);

  const auto& varchar_entry = std::get<VarcharColumnFooterEntry>(entry.Column(1));
  EXPECT_EQ(varchar_entry.Zone().MinPrefix(), "hi");
}

TEST(RowGroupFooterEntryTest, ShortFinalRowGroupIsNotRejected) {
  RowGroupFooterEntry entry(1, 780, PageRange{0, 1}, {});
  EXPECT_EQ(entry.RowCount(), 780u);
}

TEST(RowGroupFooterEntryTest, FloatColumnVariantAlsoWorks) {
  FixedWidthColumn<float> float_column;
  float_column.Append(9.5F);
  auto float_footer = FixedWidthColumnFooterEntry<float>::Build(float_column, PageRange{0, 1});

  RowGroupFooterEntry entry(1, 1, PageRange{0, 1}, {ColumnFooterEntry{float_footer}});

  ASSERT_TRUE(std::holds_alternative<FixedWidthColumnFooterEntry<float>>(entry.Column(0)));
  EXPECT_FLOAT_EQ(std::get<FixedWidthColumnFooterEntry<float>>(entry.Column(0)).Zone().Min(), 9.5F);
}

}  // namespace
}  // namespace gistdb::storage