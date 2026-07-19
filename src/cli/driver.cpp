#include "gistdb/cli/driver.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gistdb/binder/binder.hpp"
#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/binder/parser.hpp"
#include "gistdb/cli/output_formatter.hpp"
#include "gistdb/execution/insert_executor.hpp"
#include "gistdb/optimizer/optimizer.hpp"

namespace gistdb::cli {

namespace {
[[nodiscard]] bool StdoutIsTerminal() {
  return isatty(fileno(stdout)) != 0;
}

[[nodiscard]] std::string FormatElapsedSeconds(double seconds) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3) << seconds;
  return oss.str();
}

void PrintHeader(const std::vector<gistdb::binder::OutputColumn>& schema, std::ostream& out) {
  for (std::size_t i = 0; i < schema.size(); ++i) {
    if (i > 0) {
      out << '\t';
    }
    out << schema[i].display_name;
  }
  out << '\n';
}

void RunSelect(std::unique_ptr<gistdb::binder::LogicalPlanNode> plan,
               gistdb::catalog::Catalog& catalog, gistdb::storage::BufferPoolManager& buffer_pool,
               std::ostream& out) {
  std::vector<gistdb::binder::OutputColumn> schema = gistdb::binder::OutputSchema(*plan);
  auto start = std::chrono::steady_clock::now();
  std::unique_ptr<gistdb::execution::Operator> root =
      gistdb::optimizer::Optimizer::Optimize(std::move(plan), catalog, buffer_pool);

  std::uint64_t total_rows = 0;
  if (StdoutIsTerminal()) {
    std::vector<std::string> headers;
    headers.reserve(schema.size());
    for (const auto& col : schema) {
      headers.push_back(col.display_name);
    }

    std::vector<std::vector<std::string>> rows;
    while (std::optional<gistdb::execution::DataChunk> chunk = root->GetNext()) {
      for (std::uint32_t r = 0; r < chunk->RowCount(); ++r) {
        if (!chunk->IsRowSelected(r)) {
          continue;
        }
        std::vector<std::string> cells;
        cells.reserve(chunk->NumColumns());
        for (std::size_t c = 0; c < chunk->NumColumns(); ++c) {
          cells.push_back(OutputFormatter::FormatValue(chunk->Column(c), r));
        }
        rows.push_back(std::move(cells));
        ++total_rows;
      }
    }

    if (rows.empty()) {
      out << "Empty set.\n";
    } else {
      OutputFormatter::WriteTable(headers, rows, out);
    }
  } else {
    PrintHeader(schema, out);
    while (std::optional<gistdb::execution::DataChunk> chunk = root->GetNext()) {
      total_rows += chunk->CountSelectedRows();
      OutputFormatter::WriteChunk(*chunk, out);
    }
  }

  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  out << '(' << total_rows << (total_rows == 1 ? " row" : " rows") << " returned in "
      << FormatElapsedSeconds(elapsed) << "s)\n";
}

void RunInsert(const gistdb::binder::BoundInsert& insert, gistdb::catalog::Catalog& catalog,
               std::ostream& out) {
  auto start = std::chrono::steady_clock::now();
  gistdb::execution::InsertExecutor executor(catalog, insert.table_id);
  for (const auto& row : insert.rows) {
    executor.InsertRow(row);
  }
  executor.Finish();
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  out << insert.rows.size() << " row(s) inserted (" << FormatElapsedSeconds(elapsed) << "s)\n";
}

void RunCreateTable(const gistdb::binder::TableCreated& created, std::ostream& out) {
  out << "Table created (id=" << created.table_id << ").\n";
}

[[nodiscard]] std::string TypeIdName(gistdb::TypeId type) {
  switch (type) {
    case gistdb::TypeId::kInteger:
      return "INTEGER";
    case gistdb::TypeId::kFloat:
      return "FLOAT";
    case gistdb::TypeId::kVarchar:
      return "VARCHAR";
  }
  return "UNKNOWN";
}

void RunListTables(gistdb::catalog::Catalog& catalog, std::ostream& out) {
  std::vector<std::string> names = catalog.TableNames();
  if (names.empty()) {
    out << "No tables.\n";
    return;
  }
  std::vector<std::vector<std::string>> rows;
  for (const auto& name : names) {
    const auto* table = catalog.GetTable(name);
    rows.push_back(
        {name, std::to_string(table->NumColumns()), std::to_string(table->TotalRowCount())});
  }
  OutputFormatter::WriteTable({"table", "columns", "rows"}, rows, out);
}

void RunDescribeTable(gistdb::catalog::Catalog& catalog, const std::string& table_name,
                      std::ostream& out) {
  const auto* table = catalog.GetTable(table_name);
  if (table == nullptr) {
    out << "Error: unknown table '" << table_name << "'\n";
    return;
  }
  std::vector<std::vector<std::string>> rows;
  for (std::size_t i = 0; i < table->NumColumns(); ++i) {
    const auto& col = table->Column(i);
    rows.push_back({col.name, TypeIdName(col.type), std::to_string(col.ordinal)});
  }
  OutputFormatter::WriteTable({"column", "type", "ordinal"}, rows, out);
}

}  // namespace

Driver::Driver(gistdb::catalog::Catalog& catalog, gistdb::storage::BufferPoolManager& buffer_pool,
               std::ostream& out)
    : catalog_(catalog), buffer_pool_(buffer_pool), out_(out) {}

bool Driver::TryHandleMetaCommand(const std::string& input) {
  if (input == "\\dt" || input == "\\d") {
    RunListTables(catalog_, out_);
    return true;
  }
  if (input.rfind("\\d ", 0) == 0) {
    std::string table_name = input.substr(3);
    std::size_t end = table_name.find_last_not_of(" \t");
    if (end != std::string::npos) {
      table_name = table_name.substr(0, end + 1);
    }
    RunDescribeTable(catalog_, table_name, out_);
    return true;
  }
  return false;
}

void Driver::ExecuteStatement(const std::string& sql) {
  if (TryHandleMetaCommand(sql)) {
    return;
  }
  try {
    gistdb::binder::ParsedStatement parsed = gistdb::binder::Parser::ParseSingleStatement(sql);
    gistdb::binder::BindResult bound = gistdb::binder::Binder::Bind(parsed, catalog_);

    std::visit(
        [&](auto& result) {
          using T = std::decay_t<decltype(result)>;
          if constexpr (std::is_same_v<T, std::unique_ptr<gistdb::binder::LogicalPlanNode>>) {
            RunSelect(std::move(result), catalog_, buffer_pool_, out_);
          } else if constexpr (std::is_same_v<T, gistdb::binder::BoundInsert>) {
            RunInsert(std::move(result), catalog_, out_);
          } else {
            RunCreateTable(result, out_);
          }
        },
        bound);
  } catch (const std::exception& e) {
    out_ << "Error: " << e.what() << '\n';
  }
}

}  // namespace gistdb::cli