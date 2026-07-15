#include "gistdb/storage/replacer.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace gistdb::storage {
namespace {

TEST(LruReplacerTest, NewlyAccessedFrameIsNotEvictableByDefault) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  EXPECT_EQ(replacer.Size(), 0U);
}

TEST(LruReplacerTest, SetEvictableTrueIncreasesSize) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  EXPECT_EQ(replacer.Size(), 1U);
}

TEST(LruReplacerTest, SetEvictableIsIdempotent) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  replacer.SetEvictable(0, true);
  EXPECT_EQ(replacer.Size(), 1U);
}

TEST(LruReplacerTest, SetEvictableOnUnknownFrameThrows) {
  LruReplacer replacer(4);
  EXPECT_THROW(replacer.SetEvictable(99, true), std::out_of_range);
}

TEST(LruReplacerTest, EvictReturnsNulloptWhenNothingEvictable) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  EXPECT_FALSE(replacer.Evict().has_value());
}

TEST(LruReplacerTest, EvictReturnsLeastRecentlyUsedFrame) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  replacer.RecordAccess(1);
  replacer.SetEvictable(0, true);
  replacer.SetEvictable(1, true);

  auto victim = replacer.Evict();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(*victim, 0);
}

TEST(LruReplacerTest, RecordAccessRefreshesRecency) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  replacer.RecordAccess(1);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  replacer.SetEvictable(1, true);

  auto victim = replacer.Evict();
  ASSERT_TRUE(victim.has_value());
  EXPECT_EQ(*victim, 1);
}

TEST(LruReplacerTest, EvictedFrameIsForgottenEntirely) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  EXPECT_TRUE(replacer.Evict().has_value());
  EXPECT_THROW(replacer.SetEvictable(0, true), std::out_of_range);
}

TEST(LruReplacerTest, RemoveOnEvictableFrameDecreasesSize) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  replacer.Remove(0);
  EXPECT_EQ(replacer.Size(), 0U);
}

TEST(LruReplacerTest, RemoveOnNonEvictableFrameThrows) {
  LruReplacer replacer(4);
  replacer.RecordAccess(0);
  EXPECT_THROW(replacer.Remove(0), std::runtime_error);
}

TEST(LruReplacerTest, RemoveOnUnknownFrameIsNoOp) {
  LruReplacer replacer(4);
  EXPECT_NO_THROW(replacer.Remove(42));
}

}  // namespace
}  // namespace gistdb::storage