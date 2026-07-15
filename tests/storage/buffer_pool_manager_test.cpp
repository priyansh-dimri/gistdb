#include "gistdb/storage/buffer_pool_manager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/storage/disk_manager.hpp"

namespace gistdb::storage {
namespace {

using gistdb::test_utils::ScopedTempFile;

std::vector<std::uint8_t> MakePage(std::uint8_t fill_byte) {
  return std::vector<std::uint8_t>(gistdb::kPageSizeBytes, fill_byte);
}

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

TEST(BufferPoolManagerTest, FetchPageReturnsBytesWrittenToDisk) {
  ScopedTempFile temp;
  auto disk = DiskManager::CreateNew(FreshPath(temp));
  std::uint32_t page_id = disk.AllocatePages(1);
  disk.WritePages(page_id, MakePage(0x42));

  BufferPoolManager bpm(4, &disk);
  std::byte* ptr = bpm.FetchPage(page_id);
  EXPECT_EQ(static_cast<std::uint8_t>(ptr[0]), 0x42);
  EXPECT_EQ(static_cast<std::uint8_t>(ptr[gistdb::kPageSizeBytes - 1]), 0x42);
  bpm.UnpinPage(page_id, false);
}

TEST(BufferPoolManagerTest, FetchPageOnAlreadyPinnedPageReturnsSamePointer) {
  ScopedTempFile temp;
  auto disk = DiskManager::CreateNew(FreshPath(temp));
  std::uint32_t page_id = disk.AllocatePages(1);
  disk.WritePages(page_id, MakePage(0x11));

  BufferPoolManager bpm(4, &disk);
  std::byte* first = bpm.FetchPage(page_id);
  std::byte* second = bpm.FetchPage(page_id);
  EXPECT_EQ(first, second);
  bpm.UnpinPage(page_id, false);
  bpm.UnpinPage(page_id, false);
}

TEST(BufferPoolManagerTest, EvictionOccursInLruOrderAndRereadsCorrectly) {
  ScopedTempFile temp;
  auto disk = DiskManager::CreateNew(FreshPath(temp));
  std::uint32_t page_a = disk.AllocatePages(1);
  std::uint32_t page_b = disk.AllocatePages(1);
  std::uint32_t page_c = disk.AllocatePages(1);
  disk.WritePages(page_a, MakePage(0xAA));
  disk.WritePages(page_b, MakePage(0xBB));
  disk.WritePages(page_c, MakePage(0xCC));

  BufferPoolManager bpm(2, &disk);
  EXPECT_NE(bpm.FetchPage(page_a), nullptr);
  bpm.UnpinPage(page_a, false);
  EXPECT_NE(bpm.FetchPage(page_b), nullptr);
  bpm.UnpinPage(page_b, false);

  std::byte* c_ptr = bpm.FetchPage(page_c);
  EXPECT_EQ(static_cast<std::uint8_t>(c_ptr[0]), 0xCC);
  bpm.UnpinPage(page_c, false);

  std::byte* a_ptr_again = bpm.FetchPage(page_a);
  EXPECT_EQ(static_cast<std::uint8_t>(a_ptr_again[0]), 0xAA);
  bpm.UnpinPage(page_a, false);
}

TEST(BufferPoolManagerTest, FetchPageThrowsWhenPoolFullAndNothingEvictable) {
  ScopedTempFile temp;
  auto disk = DiskManager::CreateNew(FreshPath(temp));
  std::uint32_t page_a = disk.AllocatePages(1);
  std::uint32_t page_b = disk.AllocatePages(1);
  disk.WritePages(page_a, MakePage(0x01));
  disk.WritePages(page_b, MakePage(0x02));

  BufferPoolManager bpm(1, &disk);
  EXPECT_NE(bpm.FetchPage(page_a), nullptr);
  EXPECT_THROW((void)bpm.FetchPage(page_b), std::runtime_error);
}

TEST(BufferPoolManagerTest, PoolSizeReturnsConfiguredValue) {
  ScopedTempFile temp;
  auto disk = DiskManager::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(7, &disk);
  EXPECT_EQ(bpm.PoolSize(), 7U);
}

TEST(BufferPoolManagerTest, UnpinPageOnNeverFetchedPageIsSafeNoOp) {
  ScopedTempFile temp;
  auto disk = DiskManager::CreateNew(FreshPath(temp));
  BufferPoolManager bpm(2, &disk);
  EXPECT_NO_THROW(bpm.UnpinPage(999, false));
}

}  // namespace
}  // namespace gistdb::storage