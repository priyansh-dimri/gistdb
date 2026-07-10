#include "gistdb/storage/file_header.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace gistdb::storage {

namespace {
constexpr std::array<std::uint8_t, 8> kMagic = {'G', 'I', 'S', 'T', 'D', 'B', '0', '1'};

void WriteU64(std::vector<std::uint8_t>& buf, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

void WriteU32(std::vector<std::uint8_t>& buf, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

std::uint64_t ReadU64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}
}  // namespace

FileHeader::FileHeader(std::uint64_t meta_offset, std::uint32_t next_free_page_id)  // NOLINT
    : meta_offset_(meta_offset), next_free_page_id_(next_free_page_id) {}

std::vector<std::uint8_t> FileHeader::Serialize() const {
  std::vector<std::uint8_t> buf;
  buf.reserve(kSerializedSize);
  buf.insert(buf.end(), kMagic.begin(), kMagic.end());
  WriteU64(buf, meta_offset_);
  WriteU32(buf, next_free_page_id_);
  return buf;
}

FileHeader FileHeader::Deserialize(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < kSerializedSize) {
    throw std::runtime_error("FileHeader::Deserialize: buffer too small to be a valid header");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    throw std::runtime_error("FileHeader::Deserialize: magic mismatch -- not a GistDB file");
  }
  std::uint64_t meta_offset = ReadU64(bytes, kMagic.size());
  std::uint32_t next_free_page_id = ReadU32(bytes, kMagic.size() + 8);
  return {meta_offset, next_free_page_id};
}

}  // namespace gistdb::storage