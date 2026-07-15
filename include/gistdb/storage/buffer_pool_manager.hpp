#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "gistdb/storage/disk_manager.hpp"
#include "gistdb/storage/replacer.hpp"

namespace gistdb::storage {

class BufferPoolManager {  // NOLINT
 public:
  BufferPoolManager(std::size_t pool_size, DiskManager* disk_manager);
  ~BufferPoolManager();

  BufferPoolManager(const BufferPoolManager&) = delete;
  BufferPoolManager& operator=(const BufferPoolManager&) = delete;

  [[nodiscard]] std::byte* FetchPage(std::uint32_t page_id);

  void UnpinPage(std::uint32_t page_id, bool is_dirty);

  [[nodiscard]] std::size_t PoolSize() const { return pool_size_; }

 private:
  struct FrameMeta {
    std::uint32_t page_id = 0;
    std::size_t pin_count = 0;
    bool is_dirty = false;
    bool in_use = false;
  };

  [[nodiscard]] std::optional<frame_id_t> FindFreeFrame();

  std::size_t pool_size_;
  DiskManager* disk_manager_;
  std::unique_ptr<std::byte[]> pages_;  // NOLINT
  std::vector<FrameMeta> frames_;
  std::unordered_map<std::uint32_t, frame_id_t> page_table_;
  std::vector<frame_id_t> free_list_;
  LruReplacer replacer_;
};

}  // namespace gistdb::storage