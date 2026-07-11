#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "gistdb/storage/file_header.hpp"

namespace gistdb::storage {

class DiskManager {
 public:
  static DiskManager CreateNew(const std::filesystem::path& path);
  static DiskManager Open(const std::filesystem::path& path);

  ~DiskManager();

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;
  DiskManager(DiskManager&& other) noexcept;
  DiskManager& operator=(DiskManager&& other) noexcept;

  std::uint32_t AllocatePages(std::uint32_t count);
  void WritePages(std::uint32_t start_page_id, const std::vector<std::uint8_t>& data) const;
  [[nodiscard]] std::vector<std::uint8_t> ReadPages(std::uint32_t start_page_id,
                                                    std::uint32_t count) const;
  void WriteMetadataBlob(const std::vector<std::uint8_t>& blob);
  [[nodiscard]] std::vector<std::uint8_t> ReadMetadataBlob() const;

  [[nodiscard]] std::uint32_t NextFreePageId() const;

 private:
  DiskManager(int fd, FileHeader header);

  void PersistHeader();
  [[nodiscard]] std::uint64_t FileSize() const;

  int fd_ = -1;
  FileHeader header_;
};

}  // namespace gistdb::storage