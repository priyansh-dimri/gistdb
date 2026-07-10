#pragma once

#include <cstdint>

namespace gistdb::storage {
struct PageRange {
  std::uint32_t start_page_id = 0;
  std::uint32_t page_count = 0;

  friend bool operator==(const PageRange &, const PageRange &) = default;
};

} // namespace gistdb::storage