#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "gistdb/execution/data_chunk.hpp"

namespace gistdb::cli {
class OutputFormatter {
 public:
  [[nodiscard]] static std::string FormatValue(const gistdb::execution::ColumnView& column,
                                               std::uint32_t row);
  static void WriteChunk(const gistdb::execution::DataChunk& chunk, std::ostream& out);
  static void WriteTable(const std::vector<std::string>& headers,
                         const std::vector<std::vector<std::string>>& rows, std::ostream& out);
};

}  // namespace gistdb::cli