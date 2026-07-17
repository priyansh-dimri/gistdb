#include "gistdb/cli/output_formatter.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
// #include <string>

#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::cli {
namespace {

using gistdb::execution::DataChunk;
using gistdb::storage::FixedWidthColumn;
using gistdb::storage::VarcharColumn;

TEST(OutputFormatterTest, FormatsIntegerValue) {
  FixedWidthColumn<std::int32_t> col;
  col.Append(42);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "42");
}

TEST(OutputFormatterTest, FormatsNullIntegerAsLiteralNullText) {
  FixedWidthColumn<std::int32_t> col;
  col.AppendNull();
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "NULL");
}

TEST(OutputFormatterTest, FormatsFloatValueWithSixSignificantDigits) {
  FixedWidthColumn<float> col;
  col.Append(123.456F);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "123.456");
}

TEST(OutputFormatterTest, FormatsSimpleFloatValue) {
  FixedWidthColumn<float> col;
  col.Append(3.5F);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "3.5");
}

TEST(OutputFormatterTest, FormatsNullFloatAsLiteralNullText) {
  FixedWidthColumn<float> col;
  col.AppendNull();
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "NULL");
}

TEST(OutputFormatterTest, FormatsVarcharValue) {
  VarcharColumn col;
  col.Append("hello");
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "hello");
}

TEST(OutputFormatterTest, FormatsNullVarcharAsLiteralNullText) {
  VarcharColumn col;
  col.AppendNull();
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  EXPECT_EQ(OutputFormatter::FormatValue(chunk.Column(0), 0), "NULL");
}

TEST(OutputFormatterTest, WriteChunkTabSeparatesMultipleColumns) {
  FixedWidthColumn<std::int32_t> id_col;
  id_col.Append(1);
  VarcharColumn name_col;
  name_col.Append("alice");
  DataChunk chunk(1);
  chunk.AddColumn(&id_col);
  chunk.AddColumn(&name_col);

  std::ostringstream out;
  OutputFormatter::WriteChunk(chunk, out);
  EXPECT_EQ(out.str(), "1\talice\n");
}

TEST(OutputFormatterTest, WriteChunkWritesOneLinePerRow) {
  FixedWidthColumn<std::int32_t> col;
  col.Append(1);
  col.Append(2);
  col.Append(3);
  DataChunk chunk(3);
  chunk.AddColumn(&col);

  std::ostringstream out;
  OutputFormatter::WriteChunk(chunk, out);
  EXPECT_EQ(out.str(), "1\n2\n3\n");
}

TEST(OutputFormatterTest, WriteChunkSkipsUnselectedRows) {
  FixedWidthColumn<std::int32_t> col;
  col.Append(1);
  col.Append(2);
  col.Append(3);
  DataChunk chunk(3);
  chunk.AddColumn(&col);
  chunk.SetRowSelected(1, false);

  std::ostringstream out;
  OutputFormatter::WriteChunk(chunk, out);
  EXPECT_EQ(out.str(), "1\n3\n");
}

TEST(OutputFormatterTest, WriteChunkProducesEmptyOutputWhenNoRowsSelected) {
  FixedWidthColumn<std::int32_t> col;
  col.Append(1);
  DataChunk chunk(1);
  chunk.AddColumn(&col);
  chunk.SetRowSelected(0, false);

  std::ostringstream out;
  OutputFormatter::WriteChunk(chunk, out);
  EXPECT_EQ(out.str(), "");
}

TEST(OutputFormatterTest, WriteChunkHandlesNullValuesInline) {
  FixedWidthColumn<std::int32_t> col;
  col.Append(1);
  col.AppendNull();
  DataChunk chunk(2);
  chunk.AddColumn(&col);

  std::ostringstream out;
  OutputFormatter::WriteChunk(chunk, out);
  EXPECT_EQ(out.str(), "1\nNULL\n");
}

}  // namespace
}  // namespace gistdb::cli