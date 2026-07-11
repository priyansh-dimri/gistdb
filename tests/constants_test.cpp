#include "gistdb/constants.hpp"

#include <gtest/gtest.h>
namespace gistdb {
namespace {
static_assert(kRowGroupSize % kVectorSize == 0,
              "row group size must divide evenly into whole vectors");
static_assert(kPageSizeBytes > 0, "page size must be non-zero");
static_assert(kVectorSize > 0, "vector size must be non-zero");

TEST(ConstantsTest, RowGroupTilesIntoWholeVectors) {
  EXPECT_EQ(kRowGroupSize % kVectorSize, 0U);
  EXPECT_EQ(kVectorsPerRowGroup, 10U);
}

TEST(ConstantsTest, FixedWidthColumnChunkTilesPagesEvenly) {
  constexpr std::size_t chunk_bytes = kRowGroupSize * 4;
  EXPECT_EQ(chunk_bytes % kPageSizeBytes, 0U);
  EXPECT_EQ(chunk_bytes / kPageSizeBytes, 10U);
}

}  // namespace
}  // namespace gistdb