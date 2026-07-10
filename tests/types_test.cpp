#include <gtest/gtest.h>

#include "gistdb/types.hpp"

namespace gistdb {
namespace {

TEST(TypesTest, FixedWidthByteSizesMatchInt32Float32) {
  EXPECT_EQ(FixedWidthByteSize(TypeId::kInteger), 4u);
  EXPECT_EQ(FixedWidthByteSize(TypeId::kFloat), 4u);
}

TEST(TypesTest, VarcharHasNoFixedWidth) {
  EXPECT_EQ(FixedWidthByteSize(TypeId::kVarchar), std::nullopt);
}

TEST(TypesTest, IsFixedWidthClassification) {
  EXPECT_TRUE(IsFixedWidth(TypeId::kInteger));
  EXPECT_TRUE(IsFixedWidth(TypeId::kFloat));
  EXPECT_FALSE(IsFixedWidth(TypeId::kVarchar));
}

} // namespace
} // namespace gistdb