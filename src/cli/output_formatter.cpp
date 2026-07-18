#include "gistdb/cli/output_formatter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <variant>

#include "gistdb/storage/fixed_width_column.hpp"

namespace gistdb::cli {

std::string OutputFormatter::FormatValue(const gistdb::execution::ColumnView& column,
                                         std::uint32_t row) {
  return std::visit(
      [&](const auto* col) -> std::string {
        using T = std::decay_t<decltype(*col)>;
        if (col->IsNull(row)) {
          return "NULL";
        }
        if constexpr (std::is_same_v<T, gistdb::storage::FixedWidthColumn<std::int32_t>>) {
          return std::to_string(col->GetValue(row));
        } else if constexpr (std::is_same_v<T, gistdb::storage::FixedWidthColumn<float>>) {
          std::ostringstream oss;
          oss << std::setprecision(6) << col->GetValue(row);
          return oss.str();
        } else {
          return std::string(col->GetValue(row));
        }
      },
      column);
}

void OutputFormatter::WriteChunk(const gistdb::execution::DataChunk& chunk, std::ostream& out) {
  for (std::uint32_t row = 0; row < chunk.RowCount(); ++row) {
    if (!chunk.IsRowSelected(row)) {
      continue;
    }
    for (std::size_t col = 0; col < chunk.NumColumns(); ++col) {
      if (col > 0) {
        out << '\t';
      }
      out << FormatValue(chunk.Column(col), row);
    }
    out << '\n';
  }
}

void OutputFormatter::WriteTable(const std::vector<std::string>& headers,
                                 const std::vector<std::vector<std::string>>& rows,
                                 std::ostream& out) {
  std::vector<std::size_t> widths(headers.size());
  for (std::size_t c = 0; c < headers.size(); ++c) {
    widths[c] = headers[c].size();
  }
  for (const auto& row : rows) {
    for (std::size_t c = 0; c < row.size() && c < widths.size(); ++c) {
      widths[c] = std::max(widths[c], row[c].size());
    }
  }

  auto print_border = [&]() {
    for (std::size_t width : widths) {
      out << '+' << std::string(width + 2, '-');
    }
    out << "+\n";
  };
  auto print_row = [&](const std::vector<std::string>& cells) {
    for (std::size_t c = 0; c < cells.size(); ++c) {
      out << "| " << cells[c] << std::string(widths[c] - cells[c].size(), ' ') << ' ';
    }
    out << "|\n";
  };

  print_border();
  print_row(headers);
  print_border();
  for (const auto& row : rows) {
    print_row(row);
  }
  print_border();
}

}  // namespace gistdb::cli