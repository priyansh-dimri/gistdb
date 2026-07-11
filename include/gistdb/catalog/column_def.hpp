#pragma once

#include <cstdint>
#include <string>

#include "gistdb/types.hpp"

namespace gistdb::catalog {

struct ColumnDef {
  std::string name;
  TypeId type;
  std::uint32_t ordinal;

  friend bool operator==(const ColumnDef&, const ColumnDef&) = default;
};

}  // namespace gistdb::catalog