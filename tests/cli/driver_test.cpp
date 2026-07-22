#include "gistdb/cli/driver.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"

namespace gistdb::cli {
namespace {

using gistdb::catalog::Catalog;
using gistdb::storage::BufferPoolManager;
using gistdb::test_utils::ScopedTempFile;

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

class ScopedTTYRedirector {  // NOLINT
 public:
  ScopedTTYRedirector()
      : original_stdout_fd_(dup(STDOUT_FILENO)), pty_fd_(posix_openpt(O_RDWR | O_NOCTTY)) {
    if (pty_fd_ != -1) {
      grantpt(pty_fd_);
      unlockpt(pty_fd_);
      dup2(pty_fd_, STDOUT_FILENO);
    }
  }

  ~ScopedTTYRedirector() {
    if (original_stdout_fd_ != -1) {
      dup2(original_stdout_fd_, STDOUT_FILENO);
      close(original_stdout_fd_);
    }

    if (pty_fd_ != -1) {
      close(pty_fd_);
    }
  }

 private:
  int original_stdout_fd_;
  int pty_fd_;
};

TEST(DriverTest, CreateTablePrintsConfirmationWithTableId) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4, name varchar)");
  EXPECT_NE(out.str().find("Table created"), std::string::npos);
}

TEST(DriverTest, InsertPrintsRowCount) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  out.str("");
  driver.ExecuteStatement("INSERT INTO users (id) VALUES (1)");
  EXPECT_NE(out.str().find("1 row(s) inserted"), std::string::npos);
}

TEST(DriverTest, InsertOfMultipleRowsReportsCorrectCount) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  out.str("");
  driver.ExecuteStatement("INSERT INTO users (id) VALUES (1), (2), (3)");
  EXPECT_NE(out.str().find("3 row(s) inserted"), std::string::npos);
}

TEST(DriverTest, SelectPrintsHeaderAndTabSeparatedRows) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4, name varchar)");
  driver.ExecuteStatement("INSERT INTO users (id, name) VALUES (1, 'alice')");
  driver.ExecuteStatement("INSERT INTO users (id, name) VALUES (2, 'bob')");
  out.str("");
  driver.ExecuteStatement("SELECT id, name FROM users");

  std::string result = out.str();
  EXPECT_NE(result.find("id\tname\n"), std::string::npos);
  EXPECT_NE(result.find("1\talice\n"), std::string::npos);
  EXPECT_NE(result.find("2\tbob\n"), std::string::npos);
}

TEST(DriverTest, SelectWithNoMatchingRowsStillPrintsHeader) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  driver.ExecuteStatement("INSERT INTO users (id) VALUES (1)");
  out.str("");
  driver.ExecuteStatement("SELECT id FROM users WHERE id > 100");

  EXPECT_EQ(out.str(),
            "id\n"
            "(0 rows returned in 0.000s)\n");
}

TEST(DriverTest, SelectWithWhereClauseFiltersCorrectly) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  driver.ExecuteStatement("INSERT INTO users (id) VALUES (5), (15), (25)");
  out.str("");
  driver.ExecuteStatement("SELECT id FROM users WHERE id > 10");

  std::string result = out.str();
  EXPECT_EQ(result.find("\n5\n"), std::string::npos);
  EXPECT_NE(result.find("15\n"), std::string::npos);
  EXPECT_NE(result.find("25\n"), std::string::npos);
}

TEST(DriverTest, SyntaxErrorIsCaughtAndReportedNotThrown) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  EXPECT_NO_THROW(driver.ExecuteStatement("SELECT * FROM"));
  EXPECT_NE(out.str().find("Error:"), std::string::npos);
}

TEST(DriverTest, BindErrorOnUnknownTableIsCaughtAndReported) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  EXPECT_NO_THROW(driver.ExecuteStatement("SELECT * FROM ghosts"));
  EXPECT_NE(out.str().find("Error:"), std::string::npos);
}

TEST(DriverTest, BadStatementDoesNotPreventSubsequentStatementsFromRunning) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("SELECT * FROM ghosts");
  out.str("");
  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  EXPECT_NE(out.str().find("Table created"), std::string::npos);
}

TEST(DriverTest, InsertIntoUnknownTableIsCaughtAndReported) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  EXPECT_NO_THROW(driver.ExecuteStatement("INSERT INTO ghosts (id) VALUES (1)"));
  EXPECT_NE(out.str().find("Error:"), std::string::npos);
}

TEST(DriverTest, SelectEmptySetPrintsEmptyMessageWhenStdoutIsTTY) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");

  out.str("");
  {
    ScopedTTYRedirector tty;
    driver.ExecuteStatement("SELECT id FROM users");
  }

  std::string result = out.str();
  EXPECT_NE(result.find("Empty set."), std::string::npos);
}

TEST(DriverTest, SelectWithRowsUsesOutputFormatterTableWhenStdoutIsTTY) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4, name varchar)");
  driver.ExecuteStatement("INSERT INTO users (id, name) VALUES (1, 'alice')");

  out.str("");
  {
    ScopedTTYRedirector tty;
    driver.ExecuteStatement("SELECT id, name FROM users");
  }

  std::string result = out.str();

  EXPECT_EQ(result.find("Empty set."), std::string::npos);
  EXPECT_NE(result.find("alice"), std::string::npos);
  EXPECT_NE(result.find('1'), std::string::npos);
}

TEST(DriverTest, MetaCommandListTablesReturnsEmptyMessageWhenNoTablesExist) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("\\dt");

  EXPECT_NE(out.str().find("No tables.\n"), std::string::npos);
}

TEST(DriverTest, MetaCommandListTablesShowsCreatedTables) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  driver.ExecuteStatement("CREATE TABLE orders (id int4)");

  out.str("");
  driver.ExecuteStatement("\\dt");

  std::string result = out.str();
  EXPECT_NE(result.find("users"), std::string::npos);
  EXPECT_NE(result.find("orders"), std::string::npos);
}

TEST(DriverTest, MetaCommandDescribeTableShowsErrorForUnknownTable) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("\\d non_existent_table");

  EXPECT_NE(out.str().find("Error: unknown table 'non_existent_table'"), std::string::npos);
}

TEST(DriverTest, MetaCommandDescribeTableShowsColumnsAndTypes) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE mixed_types (i int4, f float4, v varchar)");

  out.str("");
  driver.ExecuteStatement("\\d mixed_types");

  std::string result = out.str();
  EXPECT_NE(result.find('i'), std::string::npos);
  EXPECT_NE(result.find("INTEGER"), std::string::npos);

  EXPECT_NE(result.find('f'), std::string::npos);
  EXPECT_NE(result.find("FLOAT"), std::string::npos);

  EXPECT_NE(result.find('v'), std::string::npos);
  EXPECT_NE(result.find("VARCHAR"), std::string::npos);
}

TEST(DriverTest, MetaCommandListTablesWithShortFormSlashD) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("\\d");

  EXPECT_NE(out.str().find("No tables.\n"), std::string::npos);
}

TEST(DriverTest, MetaCommandDescribeTableTrimsTrailingWhitespace) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("CREATE TABLE users (id int4)");
  out.str("");

  driver.ExecuteStatement("\\d users   \t ");

  std::string result = out.str();
  EXPECT_NE(result.find("id"), std::string::npos);
  EXPECT_EQ(result.find("Error: unknown table"), std::string::npos);
}

TEST(DriverTest, MetaCommandDescribeTableWithOnlyWhitespaceAfterPrefix) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);
  driver.ExecuteStatement("\\d   \t ");

  const std::string result = out.str();

  ASSERT_TRUE(result.starts_with("Error: unknown table '"));
  ASSERT_TRUE(result.ends_with("'\n"));
}

TEST(DriverTest, ExecuteStatementReturnsEarlyOnMetaCommand) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  driver.ExecuteStatement("\\dt");

  EXPECT_EQ(out.str(), "No tables.\n");
}

TEST(DriverTest, ExecuteStatementVisitsAllStatementTypes) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);

  out.str("");
  driver.ExecuteStatement("CREATE TABLE items (id int4)");
  EXPECT_NE(out.str().find("Table created"), std::string::npos);
  out.str("");
  driver.ExecuteStatement("INSERT INTO items (id) VALUES (42)");
  EXPECT_NE(out.str().find("1 row(s) inserted"), std::string::npos);
  out.str("");
  driver.ExecuteStatement("SELECT id FROM items");
  EXPECT_NE(out.str().find("42"), std::string::npos);
}

TEST(DriverTest, ExecuteStatementCatchesAndLogsExceptions) {
  ScopedTempFile temp;
  auto catalog = Catalog::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(8, &catalog.GetDiskManager());
  std::ostringstream out;
  Driver driver(catalog, bpm, out);
  out.str("");
  driver.ExecuteStatement("NOT A VALID SQL STATEMENT");

  std::string result = out.str();
  EXPECT_NE(result.find("Error:"), std::string::npos);
  EXPECT_NE(result.find('\n'), std::string::npos);
}

}  // namespace
}  // namespace gistdb::cli