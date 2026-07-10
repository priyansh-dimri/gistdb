#include "gistdb/storage/column_footer_entry.hpp"
#include <gtest/gtest.h>

namespace gistdb::storage {
namespace {

TEST(FixedWidthColumnFooterEntryTest, BuildsFromAllValidColumn) {
  FixedWidthColumn<std::int32_t> column;
  column.Append(10);
  column.Append(30);
  column.Append(5);

  auto entry =
      FixedWidthColumnFooterEntry<std::int32_t>::Build(column, PageRange{7, 3});

  EXPECT_EQ(entry.Pages(), (PageRange{7, 3}));
  EXPECT_EQ(entry.NullCount(), 0u);
  EXPECT_EQ(entry.Zone().Min(), 5);
  EXPECT_EQ(entry.Zone().Max(), 30);
}

TEST(FixedWidthColumnFooterEntryTest, NullsAreCountedAndExcludedFromZoneMap) {
  FixedWidthColumn<std::int32_t> column;
  column.Append(100);
  column.AppendNull();
  column.Append(1);

  auto entry =
      FixedWidthColumnFooterEntry<std::int32_t>::Build(column, PageRange{0, 1});

  EXPECT_EQ(entry.NullCount(), 1u);
  EXPECT_EQ(entry.Zone().Min(), 1);
  EXPECT_EQ(entry.Zone().Max(), 100);
}

TEST(FixedWidthColumnFooterEntryTest, AllNullColumnProducesVacuousZoneMap) {
  FixedWidthColumn<std::int32_t> column;
  column.AppendNull();
  column.AppendNull();

  auto entry =
      FixedWidthColumnFooterEntry<std::int32_t>::Build(column, PageRange{0, 1});

  EXPECT_EQ(entry.NullCount(), 2u);
  EXPECT_FALSE(entry.Zone().HasValues());
}

TEST(FixedWidthColumnFooterEntryTest, WorksForFloatToo) {
  FixedWidthColumn<float> column;
  column.Append(1.5F);
  column.Append(-2.5F);

  auto entry =
      FixedWidthColumnFooterEntry<float>::Build(column, PageRange{0, 1});

  EXPECT_FLOAT_EQ(entry.Zone().Min(), -2.5F);
  EXPECT_FLOAT_EQ(entry.Zone().Max(), 1.5F);
}

TEST(VarcharColumnFooterEntryTest, BuildsFromAllValidColumn) {
  VarcharColumn column;
  column.Append("banana");
  column.Append("apple");

  auto entry =
      VarcharColumnFooterEntry::Build(column, PageRange{1, 2}, PageRange{3, 4});

  EXPECT_EQ(entry.OffsetsPages(), (PageRange{1, 2}));
  EXPECT_EQ(entry.DataPages(), (PageRange{3, 4}));
  EXPECT_EQ(entry.NullCount(), 0u);
  EXPECT_EQ(entry.Zone().MinPrefix(), "apple");
  EXPECT_EQ(entry.Zone().MaxPrefix(), "banana");
}

TEST(VarcharColumnFooterEntryTest, NullsAreCountedAndExcludedFromZoneMap) {
  VarcharColumn column;
  column.Append("z");
  column.AppendNull();

  auto entry =
      VarcharColumnFooterEntry::Build(column, PageRange{0, 1}, PageRange{0, 1});

  EXPECT_EQ(entry.NullCount(), 1u);
  EXPECT_EQ(entry.Zone().MinPrefix(), "z");
}

TEST(VarcharColumnFooterEntryTest, AllNullColumnProducesVacuousZoneMap) {
  VarcharColumn column;
  column.AppendNull();

  auto entry =
      VarcharColumnFooterEntry::Build(column, PageRange{0, 1}, PageRange{0, 1});

  EXPECT_EQ(entry.NullCount(), 1u);
  EXPECT_FALSE(entry.Zone().HasValues());
}

} // namespace
} // namespace gistdb::storage