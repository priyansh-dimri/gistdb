#pragma once

#include <cstdint>
#include <vector>

#include "gistdb/storage/row_group_footer_entry.hpp"

namespace gistdb::storage {

class Footer {
 public:
  Footer() = default;

  void AddRowGroup(RowGroupFooterEntry entry);

  [[nodiscard]] std::size_t NumRowGroups() const;
  [[nodiscard]] const RowGroupFooterEntry& RowGroup(std::size_t index) const;  // pre: index < NumRowGroups()

  // serializes footer into a flat byte buffer
  [[nodiscard]] std::vector<std::uint8_t> Serialize() const;

  // constructs footer from raw bytes
  static Footer Deserialize(const std::vector<std::uint8_t>& bytes);

 private:
  std::vector<RowGroupFooterEntry> row_groups_;
};

}  // namespace gistdb::storage