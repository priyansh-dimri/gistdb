#include "gistdb/storage/file_header.hpp"

#include <gtest/gtest.h>

namespace gistdb::storage {
namespace {

TEST(FileHeaderTest, DefaultConstructionIsZeroed) {
  FileHeader header;
  EXPECT_EQ(header.MetaOffset(), 0U);
  EXPECT_EQ(header.NextFreePageId(), 0U);
}

TEST(FileHeaderTest, ConstructWithFieldsStoresThem) {
  FileHeader header(12345, 7);
  EXPECT_EQ(header.MetaOffset(), 12345U);
  EXPECT_EQ(header.NextFreePageId(), 7U);
}

TEST(FileHeaderTest, SettersUpdateFieldsIndependently) {
  FileHeader header;
  header.SetMetaOffset(999);
  EXPECT_EQ(header.MetaOffset(), 999U);
  EXPECT_EQ(header.NextFreePageId(), 0U);  // unaffected

  header.SetNextFreePageId(3);
  EXPECT_EQ(header.NextFreePageId(), 3U);
  EXPECT_EQ(header.MetaOffset(), 999U);  // unaffected
}

TEST(FileHeaderTest, SerializeProducesFixedExpectedSize) {
  FileHeader header(1, 1);
  EXPECT_EQ(header.Serialize().size(), FileHeader::kSerializedSize);
}

TEST(FileHeaderTest, RoundTripPreservesFields) {
  FileHeader header(/*meta_offset=*/9'999'999'999ULL, /*next_free_page_id=*/42);
  FileHeader restored = FileHeader::Deserialize(header.Serialize());
  EXPECT_EQ(restored.MetaOffset(), 9'999'999'999ULL);
  EXPECT_EQ(restored.NextFreePageId(), 42U);
}

TEST(FileHeaderTest, DeserializeRejectsBadMagic) {
  std::vector<std::uint8_t> bytes(FileHeader::kSerializedSize, 0);  // all zero, no valid magic
  EXPECT_THROW(FileHeader::Deserialize(bytes), std::runtime_error);
}

TEST(FileHeaderTest, DeserializeRejectsTruncatedBuffer) {
  std::vector<std::uint8_t> bytes(FileHeader::kSerializedSize - 1, 0);
  EXPECT_THROW(FileHeader::Deserialize(bytes), std::runtime_error);
}

}  // namespace
}  // namespace gistdb::storage