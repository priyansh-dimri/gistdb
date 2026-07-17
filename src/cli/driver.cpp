#include "gistdb/cli/driver.hpp"

#include <exception>
#include <optional>
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

  std::unique_ptr<gistdb::execution::Operator> root =
      gistdb::optimizer::Optimizer::Optimize(std::move(plan), catalog, buffer_pool);

  bool header_printed = false;
  while (std::optional<gistdb::execution::DataChunk> chunk = root->GetNext()) {
    if (!header_printed) {
      PrintHeader(schema, out);
      header_printed = true;
    }
    OutputFormatter::WriteChunk(*chunk, out);
  }
  if (!header_printed) {
    PrintHeader(schema, out);
  }
}

void RunInsert(const gistdb::binder::BoundInsert& insert, gistdb::catalog::Catalog& catalog,
               std::ostream& out) {
  gistdb::execution::InsertExecutor executor(catalog, insert.table_id);
  for (const auto& row : insert.rows) {
    executor.InsertRow(row);
  }
  executor.Finish();
  out << insert.rows.size() << " row(s) inserted.\n";
}

void RunCreateTable(const gistdb::binder::TableCreated& created, std::ostream& out) {
  out << "Table created (id=" << created.table_id << ").\n";
}

}  // namespace

Driver::Driver(gistdb::catalog::Catalog& catalog, gistdb::storage::BufferPoolManager& buffer_pool,
               std::ostream& out)
    : catalog_(catalog), buffer_pool_(buffer_pool), out_(out) {}

void Driver::ExecuteStatement(const std::string& sql) {
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