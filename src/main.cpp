#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

#include "gistdb/catalog/catalog.hpp"
#include "gistdb/cli/driver.hpp"
#include "gistdb/storage/buffer_pool_manager.hpp"

namespace {
constexpr std::size_t kDefaultBufferPoolFrames = 256;
constexpr const char* kPrompt = "\033[1;35mGistDB> \033[0m";
constexpr const char* kContinuationPrompt = "\033[1;35m      > \033[0m";

volatile std::sig_atomic_t g_sigint_received = 0;  // NOLINT

void HandleSigint(int /*signum*/) {
  g_sigint_received = 1;
}

void InstallSigintHandler() {
  struct sigaction sa{};
  sa.sa_handler = HandleSigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
}

bool InputAlreadyPending() {
  int bytes_available = 0;
  if (ioctl(STDIN_FILENO, FIONREAD, &bytes_available) != 0) {  // NOLINT
    return false;
  }
  return bytes_available > 0;
}

std::string Trim(const std::string& text) {
  std::size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  std::size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

enum class ReadLineStatus : uint8_t { kLine, kEof, kInterrupted };

struct ReadLineResult {
  ReadLineStatus status;
  std::string line;
};

ReadLineResult ReadLine() {
  std::string line;
  char ch = 0;
  while (true) {
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n < 0) {
      if (errno == EINTR) {
        return ReadLineResult{.status = ReadLineStatus::kInterrupted, .line = ""};
      }
      return ReadLineResult{.status = ReadLineStatus::kEof, .line = ""};
    }
    if (n == 0) {
      return ReadLineResult{.status = line.empty() ? ReadLineStatus::kEof : ReadLineStatus::kLine,
                            .line = line};
    }
    if (ch == '\n') {
      return ReadLineResult{.status = ReadLineStatus::kLine, .line = line};
    }
    line.push_back(ch);
  }
}

void PrintHelp(std::ostream& out) {
  out << "GistDB -- supported statements\n\n"
      << "  CREATE TABLE t (col TYPE, ...)     TYPE is INTEGER, FLOAT, or VARCHAR\n"
      << "  INSERT INTO t [(col, ...)] VALUES (...), (...), ...\n"
      << "  SELECT expr, ... FROM t [JOIN t2 ON t.a = t2.b] [WHERE cond]\n"
      << "                    [GROUP BY col, ...]\n\n"
      << "  Aggregates:  COUNT(*), COUNT(col), SUM(col), AVG(col), MIN(col), MAX(col)\n"
      << "  Operators:   = <> != < <= > >=   + - * /   AND OR NOT\n"
      << "  Joins:       INNER JOIN only, equality conditions only\n\n"
      << "  Not supported: HAVING, ORDER BY, LIMIT/OFFSET, DISTINCT, subqueries,\n"
      << "                 UPDATE, DELETE, ALTER TABLE, DROP TABLE, WITH, UNION\n\n"
      << "  A statement may span multiple lines and ends at the first ';'.\n\n"
      << "  help          show this message\n"
      << "  quit / exit   leave GistDB\n";
}

}  // namespace

int main(int argc, char** argv) {  // NOLINT
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
    const bool interactive = isatty(STDIN_FILENO) != 0;

    InstallSigintHandler();

    std::string statement_buffer;
    auto maybe_print_prompt = [&] {
      if (!interactive) {
        return;
      }
      if (InputAlreadyPending()) {
        return;
      }
      std::cout << (statement_buffer.empty() ? kPrompt : kContinuationPrompt) << std::flush;
    };

    maybe_print_prompt();
    while (true) {
      if (g_sigint_received) {
        g_sigint_received = 0;
        statement_buffer.clear();
        if (interactive) {
          std::cout << '\n';
        }
        maybe_print_prompt();
        continue;
      }

      ReadLineResult result = ReadLine();

      if (result.status == ReadLineStatus::kInterrupted) {
        g_sigint_received = 0;
        statement_buffer.clear();
        if (interactive) {
          std::cout << '\n';
        }
        maybe_print_prompt();
        continue;
      }
      if (result.status == ReadLineStatus::kEof) {
        if (interactive) {
          std::cout << '\n';
        }
        break;
      }

      std::string trimmed_line = Trim(result.line);
      if (statement_buffer.empty()) {
        std::string lowered = trimmed_line;
        if (!lowered.empty() && lowered.back() == ';') {
          lowered.pop_back();
        }
        lowered = Trim(lowered);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lowered == "quit" || lowered == "exit") {
          if (interactive) {
            std::cout << "Goodbye.\n";
          }
          break;
        }
        if (lowered == "help") {
          PrintHelp(std::cout);
          maybe_print_prompt();
          continue;
        }
        if (trimmed_line.empty()) {
          maybe_print_prompt();
          continue;
        }
      }

      if (!statement_buffer.empty()) {
        statement_buffer += '\n';
      }
      statement_buffer += result.line;

      std::size_t semicolon_pos{};
      while ((semicolon_pos = statement_buffer.find(';')) != std::string::npos) {
        std::string statement = Trim(statement_buffer.substr(0, semicolon_pos));
        statement_buffer.erase(0, semicolon_pos + 1);
        if (!statement.empty()) {
          driver.ExecuteStatement(statement);
        }
      }

      maybe_print_prompt();
    }
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}