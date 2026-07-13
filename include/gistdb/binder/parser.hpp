#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include "gistdb/binder/ast.hpp"

namespace gistdb::binder {

class ParseException : public std::runtime_error {
 public:
  explicit ParseException(const std::string& message) : std::runtime_error(message) {}
};

using ParsedStatement = std::variant<std::unique_ptr<SelectNode>, std::unique_ptr<InsertNode>,
                                     std::unique_ptr<CreateTableNode>>;

class Parser {
 public:
  [[nodiscard]] static ParsedStatement ParseSingleStatement(const std::string& sql);
};

}  // namespace gistdb::binder