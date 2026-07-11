#include "gistdb/serialization/byte_io.hpp"

#include <cstring>

namespace gistdb::serialization {

void WriteU8(std::vector<std::uint8_t>& buf, std::uint8_t value) {
  buf.push_back(value);
}

void WriteU32(std::vector<std::uint8_t>& buf, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

void WriteU64(std::vector<std::uint8_t>& buf, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

void WriteFloat(std::vector<std::uint8_t>& buf, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  WriteU32(buf, bits);
}

void WriteString(std::vector<std::uint8_t>& buf, const std::string& value) {
  WriteU32(buf, static_cast<std::uint32_t>(value.size()));
  buf.insert(buf.end(), value.begin(), value.end());
}

std::uint8_t ByteReader::ReadU8() {
  return bytes_[pos_++];
}

std::uint32_t ByteReader::ReadU32() {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes_[pos_++]) << (8 * i);
  }
  return value;
}

std::uint64_t ByteReader::ReadU64() {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(bytes_[pos_++]) << (8 * i);
  }
  return value;
}

float ByteReader::ReadFloat() {
  std::uint32_t bits = ReadU32();
  float value{};
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string ByteReader::ReadString() {
  std::uint32_t length = ReadU32();
  std::string value(reinterpret_cast<const char*>(bytes_.data()) + pos_, length);
  pos_ += length;
  return value;
}

std::string ByteReader::ReadFixedBytes(std::size_t length) {
  std::string value(reinterpret_cast<const char*>(bytes_.data()) + pos_, length);
  pos_ += length;
  return value;
}

}  // namespace gistdb::serialization