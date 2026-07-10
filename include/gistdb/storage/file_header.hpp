#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gistdb::storage {
class FileHeader {
 public:
  // magic (8 bytes) + meta_offset (8 bytes) + next_free_page_id (4 bytes)
  static constexpr std::size_t kSerializedSize = 20;

  FileHeader() = default;
  FileHeader(std::uint64_t meta_offset, std::uint32_t next_free_page_id);

  [[nodiscard]] std::uint64_t MetaOffset() const { return meta_offset_; }
  [[nodiscard]] std::uint32_t NextFreePageId() const { return next_free_page_id_; }

  void SetMetaOffset(std::uint64_t meta_offset) { meta_offset_ = meta_offset; }
  void SetNextFreePageId(std::uint32_t next_free_page_id) {
    next_free_page_id_ = next_free_page_id;
  }

  [[nodiscard]] std::vector<std::uint8_t> Serialize() const;

  // Throws std::runtime_error if `bytes` is too short or its magic string does not match
  static FileHeader Deserialize(const std::vector<std::uint8_t>& bytes);

 private:
  std::uint64_t meta_offset_ = 0;
  std::uint32_t next_free_page_id_ = 0;
};
}  // namespace gistdb::storage