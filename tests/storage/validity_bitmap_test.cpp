#include "gistdb/storage/validity_bitmap.hpp"

#include <gtest/gtest.h>

namespace gistdb::storage {
namespace {

TEST(ValidityBitmapTest, DefaultsToAllValid) {
  ValidityBitmap bitmap(10);
  for (std::size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(bitmap.IsValid(i));
    EXPECT_FALSE(bitmap.IsNull(i));
  }
  EXPECT_EQ(bitmap.CountNulls(), 0u);
}

TEST(ValidityBitmapTest, CanDefaultToAllNull) {
  ValidityBitmap bitmap(10, false);
  for (std::size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(bitmap.IsNull(i));
  }
  EXPECT_EQ(bitmap.CountNulls(), 10u);
}

TEST(ValidityBitmapTest, SetValidTogglesIndividualBits) {
  ValidityBitmap bitmap(8);
  bitmap.SetValid(3, false);
  bitmap.SetValid(7, false);

  EXPECT_FALSE(bitmap.IsValid(3));
  EXPECT_FALSE(bitmap.IsValid(7));
  EXPECT_TRUE(bitmap.IsValid(0));
  EXPECT_EQ(bitmap.CountNulls(), 2u);

  bitmap.SetValid(3, true);
  EXPECT_TRUE(bitmap.IsValid(3));
  EXPECT_EQ(bitmap.CountNulls(), 1u);
}

TEST(ValidityBitmapTest, PushBackGrowsSizeAndPreservesExistingBits) {
  ValidityBitmap bitmap(0);
  bitmap.PushBack(true);
  bitmap.PushBack(false);
  bitmap.PushBack(true);

  EXPECT_EQ(bitmap.Size(), 3u);
  EXPECT_TRUE(bitmap.IsValid(0));
  EXPECT_TRUE(bitmap.IsNull(1));
  EXPECT_TRUE(bitmap.IsValid(2));
  EXPECT_EQ(bitmap.CountNulls(), 1u);
}

TEST(ValidityBitmapTest, PushBackGrowsByteSizeOnlyWhenCrossingByteBoundary) {
  ValidityBitmap bitmap(8);  // already 1 full byte
  EXPECT_EQ(bitmap.ByteSize(), 1u);
  bitmap.PushBack(true);  // 9th bit should needs a 2nd byte
  EXPECT_EQ(bitmap.ByteSize(), 2u);
}

TEST(ValidityBitmapTest, SizeReflectsRowCountNotByteCount) {
  EXPECT_EQ(ValidityBitmap(13).Size(), 13u);
}

TEST(ValidityBitmapTest, NonMultipleOfEightDoesNotLeakPaddingBits) {
  ValidityBitmap bitmap(13);  // 2 backing bytes, 3 unused padding bits
  EXPECT_EQ(bitmap.ByteSize(), 2u);
  EXPECT_EQ(bitmap.CountNulls(), 0u);

  bitmap.SetValid(12, false);
  EXPECT_EQ(bitmap.CountNulls(), 1u);
}

TEST(ValidityBitmapTest, ByteSizeIsCeilingDivisionBySizeEight) {
  EXPECT_EQ(ValidityBitmap(1).ByteSize(), 1u);
  EXPECT_EQ(ValidityBitmap(8).ByteSize(), 1u);
  EXPECT_EQ(ValidityBitmap(9).ByteSize(), 2u);
  EXPECT_EQ(ValidityBitmap(1024).ByteSize(), 128u);
}

TEST(ValidityBitmapTest, DataPointerIsNonNullAndSizedCorrectly) {
  ValidityBitmap bitmap(1024);
  ASSERT_NE(bitmap.Data(), nullptr);
  EXPECT_EQ(bitmap.ByteSize(), 128u);
}

}  // namespace
}  // namespace gistdb::storage