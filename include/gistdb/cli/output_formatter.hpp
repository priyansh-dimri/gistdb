// include/gistdb/cli/output_formatter.hpp
#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "gistdb/execution/data_chunk.hpp"

namespace gistdb::cli {

// Decision B.VII.28. Deliberately minimal -- a presentation-layer concern
// living outside the execution engine's own architecture, not new engine
// scope.
class OutputFormatter {
 public:
  // NULL prints as the literal text "NULL" (psql/sqlite3-style
  // convention). Not itself a locked checkpoint decision -- B.VII.28 only
  // specifies the three non-null per-type formats -- so this is a
  // concrete, disclosed choice filling a real gap the decision left open,
  // not an invented feature.
  [[nodiscard]] static std::string FormatValue(const gistdb::execution::ColumnView& column,
                                               std::uint32_t row);

  // Tab-separated, one output row per line, honoring DataChunk's own
  // selection vector (should already be all-selected by the time a
  // Projection's output reaches here, but checked rather than assumed).
  static void WriteChunk(const gistdb::execution::DataChunk& chunk, std::ostream& out);
};

}  // namespace gistdb::cli