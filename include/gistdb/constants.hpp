#pragma once
#include <cstddef>
#include <cstdint>

namespace gistdb {
inline constexpr std::size_t kPageSizeBytes = 4096;
inline constexpr std::size_t kVectorSize = 1024;
inline constexpr std::size_t kRowGroupSize = 10240;
inline constexpr std::size_t kVectorsPerRowGroup = kRowGroupSize / kVectorSize;
inline constexpr std::size_t kZoneMapPrefixLength = 8;
inline constexpr std::uint32_t kHeaderPageId = 0;
inline constexpr std::uint32_t kFirstDataPageId = kHeaderPageId + 1;
}  // namespace gistdb