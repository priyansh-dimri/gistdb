#include "gistdb/storage/disk_manager.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "gistdb/constants.hpp"

namespace gistdb::storage {

namespace {
[[noreturn]] void ThrowSystemError(const std::string& what) {
  throw std::runtime_error(what + ": " + std::strerror(errno));
}
}  // namespace

DiskManager::DiskManager(int fd, FileHeader header) : fd_(fd), header_(header) {}

DiskManager::~DiskManager() {
  if (fd_ != -1) {
    close(fd_);
  }
}

DiskManager::DiskManager(DiskManager&& other) noexcept : fd_(other.fd_), header_(other.header_) {
  other.fd_ = -1;
}

DiskManager& DiskManager::operator=(DiskManager&& other) noexcept {
  if (this != &other) {
    if (fd_ != -1) {
      close(fd_);
    }
    fd_ = other.fd_;
    header_ = other.header_;
    other.fd_ = -1;
  }
  return *this;
}

DiskManager DiskManager::CreateNew(const std::filesystem::path& path) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  if (fd == -1) {
    ThrowSystemError("DiskManager::CreateNew: failed to create '" + path.string() + "'");
  }

  FileHeader header(/*meta_offset=*/FileHeader::kSerializedSize,
                    /*next_free_page_id=*/kFirstDataPageId);
  DiskManager manager(fd, header);
  manager.PersistHeader();
  return manager;
}

DiskManager DiskManager::Open(const std::filesystem::path& path) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  int fd = open(path.c_str(), O_RDWR);
  if (fd == -1) {
    ThrowSystemError("DiskManager::Open: failed to open '" + path.string() + "'");
  }

  std::vector<std::uint8_t> header_bytes(FileHeader::kSerializedSize);
  ssize_t read_bytes = pread(fd, header_bytes.data(), header_bytes.size(), 0);
  if (read_bytes < 0 || static_cast<std::size_t>(read_bytes) != header_bytes.size()) {
    close(fd);
    ThrowSystemError("DiskManager::Open: failed to read header from '" + path.string() + "'");
  }

  try {
    FileHeader header = FileHeader::Deserialize(header_bytes);
    return {fd, header};
  } catch (...) {
    close(fd);
    throw;
  }
}

std::uint32_t DiskManager::AllocatePages(std::uint32_t count) {
  if (count == 0) {
    throw std::invalid_argument("DiskManager::AllocatePages: count must be > 0");
  }
  std::uint32_t start_page_id = header_.NextFreePageId();
  header_.SetNextFreePageId(start_page_id + count);
  PersistHeader();
  return start_page_id;
}

void DiskManager::WritePages(std::uint32_t start_page_id,
                             const std::vector<std::uint8_t>& data) const {
  if (start_page_id == kHeaderPageId) {
    throw std::invalid_argument("DiskManager::WritePages: page 0 is reserved for the file header");
  }
  if (data.empty() || data.size() % kPageSizeBytes != 0) {
    throw std::invalid_argument(
        "DiskManager::WritePages: data size must be a non-zero multiple of the page size");
  }

  auto offset = static_cast<off_t>(static_cast<std::uint64_t>(start_page_id) * kPageSizeBytes);
  ssize_t written = pwrite(fd_, data.data(), data.size(), offset);
  if (written < 0 || static_cast<std::size_t>(written) != data.size()) {
    ThrowSystemError("DiskManager::WritePages: short or failed write");
  }
}

std::vector<std::uint8_t> DiskManager::ReadPages(std::uint32_t start_page_id,  // NOLINT
                                                 std::uint32_t count) const {
  if (count == 0) {
    throw std::invalid_argument("DiskManager::ReadPages: count must be > 0");
  }
  if (start_page_id == kHeaderPageId) {
    throw std::invalid_argument("DiskManager::ReadPages: page 0 is reserved for the file header");
  }

  std::size_t size = static_cast<std::size_t>(count) * kPageSizeBytes;
  std::vector<std::uint8_t> buffer(size);
  auto offset = static_cast<off_t>(static_cast<std::uint64_t>(start_page_id) * kPageSizeBytes);
  ssize_t bytes_read = pread(fd_, buffer.data(), size, offset);
  if (bytes_read < 0 || static_cast<std::size_t>(bytes_read) != size) {
    ThrowSystemError("DiskManager::ReadPages: short or failed read");
  }
  return buffer;
}

void DiskManager::WriteMetadataBlob(const std::vector<std::uint8_t>& blob) {
  std::uint64_t page_region_end =
      static_cast<std::uint64_t>(header_.NextFreePageId()) * kPageSizeBytes;
  std::uint64_t append_offset = std::max(FileSize(), page_region_end);

  if (!blob.empty()) {
    ssize_t written = pwrite(fd_, blob.data(), blob.size(), static_cast<off_t>(append_offset));
    if (written < 0 || static_cast<std::size_t>(written) != blob.size()) {
      ThrowSystemError("DiskManager::WriteMetadataBlob: short or failed write");
    }
  }

  header_.SetMetaOffset(append_offset);
  PersistHeader();
}

std::vector<std::uint8_t> DiskManager::ReadMetadataBlob() const {
  std::uint64_t total_size = FileSize();
  std::uint64_t offset = header_.MetaOffset();
  if (offset > total_size) {
    throw std::runtime_error(
        "DiskManager::ReadMetadataBlob: meta_offset points beyond end of file -- corrupt header");
  }

  std::uint64_t blob_size = total_size - offset;
  std::vector<std::uint8_t> buffer(blob_size);
  if (blob_size > 0) {
    ssize_t bytes_read = pread(fd_, buffer.data(), blob_size, static_cast<off_t>(offset));
    if (bytes_read < 0 || static_cast<std::uint64_t>(bytes_read) != blob_size) {
      ThrowSystemError("DiskManager::ReadMetadataBlob: short or failed read");
    }
  }
  return buffer;
}

std::uint32_t DiskManager::NextFreePageId() const {
  return header_.NextFreePageId();
}

void DiskManager::PersistHeader() {
  std::vector<std::uint8_t> bytes = header_.Serialize();
  ssize_t written = pwrite(fd_, bytes.data(), bytes.size(), 0);
  if (written < 0 || static_cast<std::size_t>(written) != bytes.size()) {
    ThrowSystemError("DiskManager::PersistHeader: failed to persist file header");
  }
}

std::uint64_t DiskManager::FileSize() const {
  struct stat st {};
  if (fstat(fd_, &st) != 0) {
    ThrowSystemError("DiskManager::FileSize: fstat failed");
  }
  return static_cast<std::uint64_t>(st.st_size);
}

}  // namespace gistdb::storage