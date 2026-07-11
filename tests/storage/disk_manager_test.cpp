#include "gistdb/storage/disk_manager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/storage/file_header.hpp"

namespace gistdb::storage {
namespace {

using gistdb::test_utils::ScopedTempFile;

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

TEST(DiskManagerTest, CreateNewBootstrapsEmptyDatabase) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));

  EXPECT_EQ(manager.NextFreePageId(), kFirstDataPageId);
  EXPECT_TRUE(manager.ReadMetadataBlob().empty());
}

TEST(DiskManagerTest, CreateNewThrowsIfFileAlreadyExists) {
  ScopedTempFile temp;
  EXPECT_THROW(DiskManager::CreateNew(temp.Path()), std::runtime_error);
}

TEST(DiskManagerTest, OpenThrowsIfFileDoesNotExist) {
  ScopedTempFile temp;
  std::filesystem::path missing_path = FreshPath(temp);
  EXPECT_THROW(DiskManager::Open(missing_path), std::runtime_error);
}

TEST(DiskManagerTest, OpenRejectsFileWithBadMagic) {
  ScopedTempFile temp;
  {
    std::ofstream out(temp.Path(), std::ios::binary);
    std::vector<char> garbage(FileHeader::kSerializedSize, '\0');
    out.write(garbage.data(), static_cast<std::streamsize>(garbage.size()));
  }
  EXPECT_THROW(DiskManager::Open(temp.Path()), std::runtime_error);
}

TEST(DiskManagerTest, AllocatePagesReturnsSequentialContiguousStarts) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));

  std::uint32_t first = manager.AllocatePages(3);
  std::uint32_t second = manager.AllocatePages(2);

  EXPECT_EQ(first, kFirstDataPageId);
  EXPECT_EQ(second, kFirstDataPageId + 3);
  EXPECT_EQ(manager.NextFreePageId(), kFirstDataPageId + 5);
}

TEST(DiskManagerTest, AllocatePagesRejectsZeroCount) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));
  EXPECT_THROW(manager.AllocatePages(0), std::invalid_argument);
}

TEST(DiskManagerTest, WriteThenReadPagesRoundTrips) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));

  std::uint32_t start = manager.AllocatePages(2);
  std::vector<std::uint8_t> data(2 * kPageSizeBytes);
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<std::uint8_t>(i % 256);
  }

  manager.WritePages(start, data);
  std::vector<std::uint8_t> read_back = manager.ReadPages(start, 2);

  EXPECT_EQ(read_back, data);
}

TEST(DiskManagerTest, WritePagesRejectsSizeNotMultipleOfPageSize) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));
  std::uint32_t start = manager.AllocatePages(1);

  std::vector<std::uint8_t> bad_data(kPageSizeBytes - 1);
  EXPECT_THROW(manager.WritePages(start, bad_data), std::invalid_argument);
}

TEST(DiskManagerTest, WritePagesRejectsHeaderPageZero) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));
  std::vector<std::uint8_t> data(kPageSizeBytes);
  EXPECT_THROW(manager.WritePages(kHeaderPageId, data), std::invalid_argument);
}

TEST(DiskManagerTest, WriteMetadataBlobThenReadRoundTrips) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));

  std::vector<std::uint8_t> blob = {1, 2, 3, 4, 5};
  manager.WriteMetadataBlob(blob);

  EXPECT_EQ(manager.ReadMetadataBlob(), blob);
}

TEST(DiskManagerTest, WriteMetadataBlobGrowsMonotonicallyAcrossCalls) {
  ScopedTempFile temp;
  DiskManager manager = DiskManager::CreateNew(FreshPath(temp));

  manager.WriteMetadataBlob(std::vector<std::uint8_t>{1, 2, 3});
  std::vector<std::uint8_t> bigger_blob = {9, 8, 7, 6, 5, 4};
  manager.WriteMetadataBlob(bigger_blob);
  EXPECT_EQ(manager.ReadMetadataBlob(), bigger_blob);
}

TEST(DiskManagerTest, StatePersistsAcrossCloseAndReopen) {
  ScopedTempFile temp;
  std::filesystem::path path = FreshPath(temp);

  {
    DiskManager manager = DiskManager::CreateNew(path);
    std::uint32_t start = manager.AllocatePages(2);
    std::vector<std::uint8_t> data(2 * kPageSizeBytes, 0x42);
    manager.WritePages(start, data);
    manager.WriteMetadataBlob(std::vector<std::uint8_t>{10, 20, 30});
  }

  DiskManager reopened = DiskManager::Open(path);
  EXPECT_EQ(reopened.NextFreePageId(), kFirstDataPageId + 2);
  EXPECT_EQ(reopened.ReadMetadataBlob(), (std::vector<std::uint8_t>{10, 20, 30}));

  std::vector<std::uint8_t> read_back = reopened.ReadPages(kFirstDataPageId, 2);
  EXPECT_EQ(read_back, std::vector<std::uint8_t>(2 * kPageSizeBytes, 0x42));
}

}  // namespace
}  // namespace gistdb::storage