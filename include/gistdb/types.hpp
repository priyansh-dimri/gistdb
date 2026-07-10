#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gistdb {

enum class TypeId : std::uint8_t {
  kInteger,
  kFloat,
  kVarchar,
};

// Per-value byte width for fixed-width types; nullopt for VARCHAR,
// which has no fixed per-value size.
constexpr std::optional<std::size_t> FixedWidthByteSize(TypeId type) {
  switch (type) {
    case TypeId::kInteger:
    case TypeId::kFloat:
      return 4;
    case TypeId::kVarchar:
      return std::nullopt;
  }
  return std::nullopt;
}

constexpr bool IsFixedWidth(TypeId type) {
  return FixedWidthByteSize(type).has_value();
}

}  // namespace gistdb