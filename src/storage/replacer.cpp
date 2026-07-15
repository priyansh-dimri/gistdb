#include "gistdb/storage/replacer.hpp"

#include <stdexcept>

namespace gistdb::storage {

LruReplacer::LruReplacer(std::size_t /*num_frames*/) {}

void LruReplacer::RecordAccess(frame_id_t frame_id, page_id_t /*page_id*/,
                               AccessType /*access_type*/) {
  auto pos_it = position_.find(frame_id);
  if (pos_it != position_.end()) {
    lru_order_.erase(pos_it->second);
  } else {
    entries_.emplace(frame_id, FrameEntry{});
  }
  lru_order_.push_back(frame_id);
  position_[frame_id] = std::prev(lru_order_.end());
}

void LruReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    throw std::out_of_range("LruReplacer::SetEvictable: unknown frame_id");
  }
  if (it->second.evictable == set_evictable) {
    return;
  }
  it->second.evictable = set_evictable;
  evictable_count_ += set_evictable ? 1 : -1;
}

std::optional<frame_id_t> LruReplacer::Evict() {
  for (auto it = lru_order_.begin(); it != lru_order_.end(); ++it) {
    if (entries_.at(*it).evictable) {
      frame_id_t victim = *it;
      lru_order_.erase(it);
      position_.erase(victim);
      entries_.erase(victim);
      --evictable_count_;
      return victim;
    }
  }
  return std::nullopt;
}

void LruReplacer::Remove(frame_id_t frame_id) {
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }
  if (!it->second.evictable) {
    throw std::runtime_error("LruReplacer::Remove: frame is not evictable");
  }
  lru_order_.erase(position_.at(frame_id));
  position_.erase(frame_id);
  entries_.erase(frame_id);
  --evictable_count_;
}

}  // namespace gistdb::storage