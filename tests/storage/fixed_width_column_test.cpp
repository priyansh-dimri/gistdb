#include "gistdb/storage/fixed_width_column.hpp"

#include <gtest/gtest.h>

namespace gistdb::storage {
namespace {

TEST(FixedWidthColumnTest, DefaultConstructionIsEmpty) {
  FixedWidthColumn<std::int32_t> column;
  EXPECT_EQ(column.Size(), 0u);
}

TEST(FixedWidthColumnTest, AppendStoresValueAndMarksValid) {
  FixedWidthColumn<std::int32_t> column;
  column.Append(42);
  EXPECT_EQ(column.Size(), 1u);
  EXPECT_EQ(column.GetValue(0), 42);
  EXPECT_TRUE(column.IsValid(0));
}

TEST(FixedWidthColumnTest, AppendNullIncrementsSizeAndMarksInvalid) {
  FixedWidthColumn<std::int32_t> column;
  column.Append(1);
  column.AppendNull();
  EXPECT_EQ(column.Size(), 2u);
  EXPECT_TRUE(column.IsNull(1));
  EXPECT_FALSE(column.IsNull(0));
}

TEST(FixedWidthColumnTest, SetValueOverwritesAndMarksValid) {
  FixedWidthColumn<std::int32_t> column;
  column.AppendNull();
  column.SetValue(0, 99);
  EXPECT_EQ(column.GetValue(0), 99);
  EXPECT_TRUE(column.IsValid(0));
}

TEST(FixedWidthColumnTest, SetNullMarksInvalidWithoutChangingSize) {
  FixedWidthColumn<std::int32_t> column;
  column.Append(5);
  column.SetNull(0, true);
  EXPECT_EQ(column.Size(), 1u);
  EXPECT_TRUE(column.IsNull(0));
}

TEST(FixedWidthColumnTest, DataPointerMatchesAppendedValues) {
  FixedWidthColumn<std::int32_t> column;
  column.Append(1);
  column.Append(2);
  column.Append(3);
  const std::int32_t* data = column.Data();
  EXPECT_EQ(data[0], 1);
  EXPECT_EQ(data[1], 2);
  EXPECT_EQ(data[2], 3);
}

TEST(FixedWidthColumnTest, WorksForFloatToo) {
  FixedWidthColumn<float> column;
  column.Append(1.5F);
  column.AppendNull();
  EXPECT_FLOAT_EQ(column.GetValue(0), 1.5F);
  EXPECT_TRUE(column.IsNull(1));
}

}  // namespace
}  // namespace gistdb::storage