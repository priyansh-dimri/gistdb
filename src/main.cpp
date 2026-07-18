#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#include "gistdb/catalog/catalog.hpp"
#include "gistdb/cli/driver.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"

namespace {
constexpr std::size_t kDefaultBufferPoolFrames = 256;
constexpr const char* kPrompt = "\033[1;35mGistDB>\033[0m ";
std::string Trim(const std::string& line) {
  std::size_t begin = line.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  std::size_t end = line.find_last_not_of(" \t\r\n");
  return line.substr(begin, end - begin + 1);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <database-file>\n";
    return 1;
  }
  std::filesystem::path db_path = argv[1];

  try {
    gistdb::catalog::Catalog catalog = std::filesystem::exists(db_path)
                                           ? gistdb::catalog::Catalog::Open(db_path)
                                           : gistdb::catalog::Catalog::CreateNew(db_path);
    gistdb::storage::BufferPoolManager buffer_pool(kDefaultBufferPoolFrames,
                                                   &catalog.GetDiskManager());
    gistdb::cli::Driver driver(catalog, buffer_pool, std::cout);
    const bool interactive = isatty(fileno(stdin)) != 0;

    std::string line;
    if (interactive) {
      std::cout << kPrompt << std::flush;
    }
    while (std::getline(std::cin, line)) {
      std::string statement = Trim(line);
      if (!statement.empty() && statement.back() == ';') {
        statement.pop_back();
      }
      if (!statement.empty()) {
        driver.ExecuteStatement(statement);
      }
      if (interactive) {
        std::cout << kPrompt << std::flush;
      }
    }
    if (interactive) {
      std::cout << '\n';
    }
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}