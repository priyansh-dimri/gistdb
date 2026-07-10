#include <gtest/gtest.h>
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::storage
{
    namespace
    {

        TEST(VarcharColumnTest, DefaultConstructionIsEmpty)
        {
            VarcharColumn column;
            EXPECT_EQ(column.Size(), 0u);
            EXPECT_EQ(column.NumOffsets(), 1u); // offsets_ always starts with [0]
        }

        TEST(VarcharColumnTest, AppendStoresRetrievableValue)
        {
            VarcharColumn column;
            column.Append("hello");
            EXPECT_EQ(column.Size(), 1u);
            EXPECT_EQ(column.GetValue(0), "hello");
        }

        TEST(VarcharColumnTest, MultipleAppendsReconstructCorrectly)
        {
            VarcharColumn column;
            column.Append("ab");
            column.Append("cde");
            column.Append("f");
            EXPECT_EQ(column.GetValue(0), "ab");
            EXPECT_EQ(column.GetValue(1), "cde");
            EXPECT_EQ(column.GetValue(2), "f");
        }

        TEST(VarcharColumnTest, EmptyStringIsValidNotNull)
        {
            VarcharColumn column;
            column.Append("");
            EXPECT_FALSE(column.IsNull(0));
            EXPECT_EQ(column.GetValue(0), "");
        }

        TEST(VarcharColumnTest, AppendNullMarksInvalidWithZeroLengthSpan)
        {
            VarcharColumn column;
            column.Append("x");
            column.AppendNull();
            EXPECT_EQ(column.Size(), 2u);
            EXPECT_TRUE(column.IsNull(1));
            EXPECT_EQ(column.DataBufferSize(), 1u); // null contributes 0 bytes
        }

        TEST(VarcharColumnTest, OffsetsArrayHasSizePlusOneEntries)
        {
            VarcharColumn column;
            column.Append("ab");
            column.Append("cde");
            EXPECT_EQ(column.NumOffsets(), column.Size() + 1);
            EXPECT_EQ(column.Offsets()[0], 0);
            EXPECT_EQ(column.Offsets()[1], 2);
            EXPECT_EQ(column.Offsets()[2], 5);
        }

        TEST(VarcharColumnTest, DataBufferSizeMatchesCumulativeAppendedBytes)
        {
            VarcharColumn column;
            column.Append("ab");
            column.AppendNull();
            column.Append("cde");
            EXPECT_EQ(column.DataBufferSize(), 5u); // "ab" + "cde", null contributes 0
        }

    } // namespace
} // namespace gistdb::storage