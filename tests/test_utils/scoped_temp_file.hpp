#pragma once

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace gistdb::test_utils {

// atomically creates an empty temporary file on construction and guarantees its deletion
class ScopedTempFile {
 public:
  ScopedTempFile() {
    std::string tmpl = (std::filesystem::temp_directory_path() / "gistdb_test_XXXXXX").string();
    int fd = mkstemp(tmpl.data());
    if (fd == -1) {
      throw std::runtime_error("ScopedTempFile: mkstemp failed to create a temp file");
    }
    close(fd);
    path_ = tmpl;
  }

  ~ScopedTempFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  ScopedTempFile(const ScopedTempFile&) = delete;
  ScopedTempFile& operator=(const ScopedTempFile&) = delete;
  ScopedTempFile(ScopedTempFile&&) = delete;
  ScopedTempFile& operator=(ScopedTempFile&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace gistdb::test_utils