#include "gistdb/cli/driver.hpp"

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

  driver.ExecuteStatement("SELECT * FROM ghosts");  // fails
  out.str("");
  driver.ExecuteStatement("CREATE TABLE users (id int4)");  // must still succeed
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

}  // namespace
}  // namespace gistdb::cli