#include "gistdb/storage/file_header.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "gistdb/serialization/byte_io.hpp"

namespace gistdb::storage {

namespace {
constexpr std::array<std::uint8_t, 8> kMagic = {'G', 'I', 'S', 'T', 'D', 'B', '0', '1'};
}  // namespace

FileHeader::FileHeader(std::uint64_t meta_offset, std::uint32_t next_free_page_id)  // NOLINT
    : meta_offset_(meta_offset), next_free_page_id_(next_free_page_id) {}

std::vector<std::uint8_t> FileHeader::Serialize() const {
  std::vector<std::uint8_t> buf;
  buf.reserve(kSerializedSize);
  buf.insert(buf.end(), kMagic.begin(), kMagic.end());
  gistdb::serialization::WriteU64(buf, meta_offset_);
  gistdb::serialization::WriteU32(buf, next_free_page_id_);
  return buf;
}

FileHeader FileHeader::Deserialize(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < kSerializedSize) {
    throw std::runtime_error("FileHeader::Deserialize: buffer too small to be a valid header");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    throw std::runtime_error("FileHeader::Deserialize: magic mismatch -- not a GistDB file");
  }

  gistdb::serialization::ByteReader reader(bytes);
  reader.Skip(kMagic.size());
  std::uint64_t meta_offset = reader.ReadU64();
  std::uint32_t next_free_page_id = reader.ReadU32();
  return {meta_offset, next_free_page_id};
}

}  // namespace gistdb::storage