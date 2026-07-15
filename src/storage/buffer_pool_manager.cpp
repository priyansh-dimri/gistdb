#include "gistdb/storage/buffer_pool_manager.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

#include "gistdb/constants.hpp"

namespace gistdb::storage {

BufferPoolManager::BufferPoolManager(std::size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size),
      disk_manager_(disk_manager),
      pages_(std::make_unique<std::byte[]>(pool_size * kPageSizeBytes)),  // NOLINT
      frames_(pool_size),
      replacer_(pool_size) {
  free_list_.reserve(pool_size);
  for (std::size_t i = 0; i < pool_size; ++i) {
    free_list_.push_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() {
  for (std::size_t i = 0; i < frames_.size(); ++i) {
    if (frames_[i].in_use && frames_[i].is_dirty) {
      std::vector<std::uint8_t> buffer(kPageSizeBytes);
      std::memcpy(buffer.data(), pages_.get() + (i * kPageSizeBytes), kPageSizeBytes);
      disk_manager_->WritePages(frames_[i].page_id, buffer);
    }
  }
}

std::optional<frame_id_t> BufferPoolManager::FindFreeFrame() {
  if (!free_list_.empty()) {
    frame_id_t frame = free_list_.back();
    free_list_.pop_back();
    return frame;
  }
  return replacer_.Evict();
}

std::byte* BufferPoolManager::FetchPage(std::uint32_t page_id) {
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    frame_id_t frame = it->second;
    frames_[frame].pin_count += 1;
    replacer_.RecordAccess(frame, page_id);
    replacer_.SetEvictable(frame, false);
    return pages_.get() + (static_cast<std::size_t>(frame) * kPageSizeBytes);
  }

  std::optional<frame_id_t> maybe_frame = FindFreeFrame();
  if (!maybe_frame.has_value()) {
    throw std::runtime_error(
        "BufferPoolManager::FetchPage: no free or evictable frame -- every frame pinned "
        "simultaneously, which shouldn't happen for a single-threaded sequential scan");
  }
  frame_id_t frame = *maybe_frame;

  if (frames_[frame].in_use) {
    if (frames_[frame].is_dirty) {
      std::vector<std::uint8_t> buffer(kPageSizeBytes);
      std::memcpy(buffer.data(), pages_.get() + (static_cast<std::size_t>(frame) * kPageSizeBytes),
                  kPageSizeBytes);
      disk_manager_->WritePages(frames_[frame].page_id, buffer);
    }
    page_table_.erase(frames_[frame].page_id);
  }

  std::vector<std::uint8_t> data = disk_manager_->ReadPages(page_id, 1);
  std::memcpy(pages_.get() + (static_cast<std::size_t>(frame) * kPageSizeBytes), data.data(),
              kPageSizeBytes);
  frames_[frame] = FrameMeta{page_id, 1, false, true};
  page_table_[page_id] = frame;
  replacer_.RecordAccess(frame, page_id);
  replacer_.SetEvictable(frame, false);
  return pages_.get() + (static_cast<std::size_t>(frame) * kPageSizeBytes);
}

void BufferPoolManager::UnpinPage(std::uint32_t page_id, bool is_dirty) {
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return;
  }
  frame_id_t frame = it->second;
  if (frames_[frame].pin_count == 0) {
    return;
  }
  frames_[frame].pin_count -= 1;
  frames_[frame].is_dirty = frames_[frame].is_dirty || is_dirty;
  if (frames_[frame].pin_count == 0) {
    replacer_.SetEvictable(frame, true);
  }
}

}  // namespace gistdb::storage