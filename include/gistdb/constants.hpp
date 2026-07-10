#pragma once
#include <cstddef>

namespace gistdb
{
    inline constexpr std::size_t kPageSizeBytes = 4096;
    inline constexpr std::size_t kVectorSize = 1024;
    inline constexpr std::size_t kRowGroupSize = 10240;
    inline constexpr std::size_t kVectorsPerRowGroup = kRowGroupSize / kVectorSize;
} // namespace gistdb