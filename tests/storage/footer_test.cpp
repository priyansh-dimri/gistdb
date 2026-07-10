#include "gistdb/storage/footer.hpp"

#include <gtest/gtest.h>

namespace gistdb::storage {
namespace {

RowGroupFooterEntry MakeSampleRowGroup(std::uint32_t table_id, std::uint32_t row_count) {
  FixedWidthColumn<std::int32_t> int_column;
  int_column.Append(10);
  int_column.AppendNull();
  int_column.Append(3);
  auto int_entry = FixedWidthColumnFooterEntry<std::int32_t>::Build(int_column, PageRange{0, 10});

  FixedWidthColumn<float> float_column;
  float_column.AppendNull();  // all-null -> vacuous zone map
  auto float_entry = FixedWidthColumnFooterEntry<float>::Build(float_column, PageRange{10, 10});

  VarcharColumn varchar_column;
  varchar_column.Append("banana");
  varchar_column.Append("apple");
  auto varchar_entry =
      VarcharColumnFooterEntry::Build(varchar_column, PageRange{20, 10}, PageRange{30, 5});

  return RowGroupFooterEntry(table_id, row_count, PageRange{40, 3},
                             {ColumnFooterEntry{int_entry}, ColumnFooterEntry{float_entry},
                              ColumnFooterEntry{varchar_entry}});
}

TEST(FooterTest, EmptyFooterRoundTrips) {
  Footer footer;
  Footer restored = Footer::Deserialize(footer.Serialize());
  EXPECT_EQ(restored.NumRowGroups(), 0u);
}

TEST(FooterTest, SingleRowGroupTopLevelFieldsRoundTrip) {
  Footer footer;
  footer.AddRowGroup(MakeSampleRowGroup(7, 10240));
  Footer restored = Footer::Deserialize(footer.Serialize());

  ASSERT_EQ(restored.NumRowGroups(), 1u);
  const auto& rg = restored.RowGroup(0);
  EXPECT_EQ(rg.TableId(), 7u);
  EXPECT_EQ(rg.RowCount(), 10240u);
  EXPECT_EQ(rg.ValidityBitmapRegion(), (PageRange{40, 3}));
  ASSERT_EQ(rg.NumColumns(), 3u);
}

TEST(FooterTest, FixedWidthColumnFieldsRoundTrip) {
  Footer footer;
  footer.AddRowGroup(MakeSampleRowGroup(1, 100));
  Footer restored = Footer::Deserialize(footer.Serialize());

  const auto& int_entry =
      std::get<FixedWidthColumnFooterEntry<std::int32_t>>(restored.RowGroup(0).Column(0));
  EXPECT_EQ(int_entry.Pages(), (PageRange{0, 10}));
  EXPECT_EQ(int_entry.NullCount(), 1u);
  ASSERT_TRUE(int_entry.Zone().HasValues());
  EXPECT_EQ(int_entry.Zone().Min(), 3);
  EXPECT_EQ(int_entry.Zone().Max(), 10);
}

TEST(FooterTest, AllNullColumnPreservesVacuousZoneMapAcrossRoundTrip) {
  Footer footer;
  footer.AddRowGroup(MakeSampleRowGroup(1, 100));
  Footer restored = Footer::Deserialize(footer.Serialize());

  const auto& float_entry =
      std::get<FixedWidthColumnFooterEntry<float>>(restored.RowGroup(0).Column(1));
  EXPECT_EQ(float_entry.NullCount(), 1u);
  EXPECT_FALSE(float_entry.Zone().HasValues());
}

TEST(FooterTest, VarcharColumnFieldsRoundTrip) {
  Footer footer;
  footer.AddRowGroup(MakeSampleRowGroup(1, 100));
  Footer restored = Footer::Deserialize(footer.Serialize());

  const auto& varchar_entry = std::get<VarcharColumnFooterEntry>(restored.RowGroup(0).Column(2));
  EXPECT_EQ(varchar_entry.OffsetsPages(), (PageRange{20, 10}));
  EXPECT_EQ(varchar_entry.DataPages(), (PageRange{30, 5}));
  EXPECT_EQ(varchar_entry.NullCount(), 0u);
  EXPECT_EQ(varchar_entry.Zone().MinPrefix(), "apple");
  EXPECT_EQ(varchar_entry.Zone().MaxPrefix(), "banana");
}

TEST(FooterTest, PrefixLongerThanZoneMapPrefixLengthRoundTripsTruncatedExactly) {
  VarcharColumn column;
  column.Append("abcdefghijklmnop");  // longer than kZoneMapPrefixLength
  auto entry = VarcharColumnFooterEntry::Build(column, PageRange{0, 1}, PageRange{0, 1});

  Footer footer;
  footer.AddRowGroup(RowGroupFooterEntry(1, 1, PageRange{0, 1}, {ColumnFooterEntry{entry}}));
  Footer restored = Footer::Deserialize(footer.Serialize());

  const auto& restored_entry = std::get<VarcharColumnFooterEntry>(restored.RowGroup(0).Column(0));
  EXPECT_EQ(restored_entry.Zone().MinPrefix(), "abcdefgh");
}

TEST(FooterTest, MultipleRowGroupsPreserveOrderAndIndependentContent) {
  Footer footer;
  footer.AddRowGroup(MakeSampleRowGroup(1, 10240));
  footer.AddRowGroup(MakeSampleRowGroup(2, 780));  // short final row group, Decision 5.7

  Footer restored = Footer::Deserialize(footer.Serialize());

  ASSERT_EQ(restored.NumRowGroups(), 2u);
  EXPECT_EQ(restored.RowGroup(0).TableId(), 1u);
  EXPECT_EQ(restored.RowGroup(0).RowCount(), 10240u);
  EXPECT_EQ(restored.RowGroup(1).TableId(), 2u);
  EXPECT_EQ(restored.RowGroup(1).RowCount(), 780u);
}

}  // namespace
}  // namespace gistdb::storage