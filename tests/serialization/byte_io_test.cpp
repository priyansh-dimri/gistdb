#include "gistdb/serialization/byte_io.hpp"

#include <gtest/gtest.h>

namespace gistdb::serialization {
namespace {

TEST(ByteIoTest, U8RoundTrips) {
  std::vector<std::uint8_t> buf;
  WriteU8(buf, 200);
  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadU8(), 200);
}

TEST(ByteIoTest, U32RoundTrips) {
  std::vector<std::uint8_t> buf;
  WriteU32(buf, 4'000'000'000U);
  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadU32(), 4'000'000'000U);
}

TEST(ByteIoTest, U64RoundTrips) {
  std::vector<std::uint8_t> buf;
  WriteU64(buf, 10'000'000'000'000ULL);
  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadU64(), 10'000'000'000'000ULL);
}

TEST(ByteIoTest, FloatRoundTrips) {
  std::vector<std::uint8_t> buf;
  WriteFloat(buf, -3.25F);
  ByteReader reader(buf);
  EXPECT_FLOAT_EQ(reader.ReadFloat(), -3.25F);
}

TEST(ByteIoTest, StringRoundTrips) {
  std::vector<std::uint8_t> buf;
  WriteString(buf, "hello world");
  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadString(), "hello world");
}

TEST(ByteIoTest, EmptyStringRoundTrips) {
  std::vector<std::uint8_t> buf;
  WriteString(buf, "");
  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadString(), "");
}

TEST(ByteIoTest, SequentialWritesReadBackInOrder) {
  std::vector<std::uint8_t> buf;
  WriteU8(buf, 1);
  WriteU32(buf, 2);
  WriteString(buf, "three");
  WriteFloat(buf, 4.5F);

  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadU8(), 1);
  EXPECT_EQ(reader.ReadU32(), 2U);
  EXPECT_EQ(reader.ReadString(), "three");
  EXPECT_FLOAT_EQ(reader.ReadFloat(), 4.5F);
}

TEST(ByteIoTest, ReadFixedBytesReadsExactLengthWithoutPrefix) {
  std::vector<std::uint8_t> buf;
  WriteU8(buf, 'a');
  WriteU8(buf, 'b');
  WriteU8(buf, 'c');
  ByteReader reader(buf);
  EXPECT_EQ(reader.ReadFixedBytes(3), "abc");
}

TEST(ByteIoTest, SkipAdvancesPastGivenByteCount) {
  std::vector<std::uint8_t> buf;
  WriteU8(buf, 0xFF);
  WriteU8(buf, 0xFF);
  WriteU32(buf, 42);
  ByteReader reader(buf);
  reader.Skip(2);
  EXPECT_EQ(reader.ReadU32(), 42U);
}

}  // namespace
}  // namespace gistdb::serialization