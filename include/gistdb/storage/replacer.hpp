// include/gistdb/storage/replacer.hpp
#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace gistdb::storage {

using frame_id_t = std::int32_t;
using page_id_t = std::uint32_t;

enum class AccessType : std::uint8_t { kUnknown, kLookup, kScan };

class LruReplacer {
 public:
  explicit LruReplacer(std::size_t num_frames);

  void RecordAccess(frame_id_t frame_id, page_id_t page_id = 0,
                    AccessType access_type = AccessType::kUnknown);

  void SetEvictable(frame_id_t frame_id, bool set_evictable);

  [[nodiscard]] std::optional<frame_id_t> Evict();
  void Remove(frame_id_t frame_id);

  [[nodiscard]] std::size_t Size() const { return evictable_count_; }

 private:
  struct FrameEntry {
    bool evictable = false;
  };

  std::size_t evictable_count_ = 0;
  std::list<frame_id_t> lru_order_;  // front = least recently used
  std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> position_;
  std::unordered_map<frame_id_t, FrameEntry> entries_;
};

}  // namespace gistdb::storage