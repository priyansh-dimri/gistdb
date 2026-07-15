#include "gistdb/execution/seq_scan_operator.hpp"

#include <cstring>
#include <iostream>
#include <type_traits>

#include "gistdb/constants.hpp"
#include "gistdb/storage/column_footer_entry.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/row_group_footer_entry.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {

namespace {

[[nodiscard]] bool IsColumnValidAtRow(const std::byte* bitmap_region,
                                      std::uint32_t row_group_row_count,  // NOLINT
                                      std::uint32_t column_ordinal, std::uint32_t row_in_group) {
  const std::size_t bytes_per_column = (row_group_row_count + 7) / 8;
  const std::byte* column_bytes = bitmap_region + (column_ordinal * bytes_per_column);
  const auto byte = static_cast<std::uint8_t>(column_bytes[row_in_group / 8]);
  return ((byte >> (row_in_group % 8)) & 1U) != 0;
}

[[nodiscard]] bool ZoneMapRulesOutRowGroup(const gistdb::storage::RowGroupFooterEntry& row_group,
                                           const ZoneMapSkipCondition& condition) {
  return std::visit(
      [&](const auto& constant) -> bool {
        using T = std::decay_t<decltype(constant)>;
        const auto& entry = std::get<gistdb::storage::FixedWidthColumnFooterEntry<T>>(
            row_group.Column(condition.ordinal));
        if (!entry.Zone().HasValues()) {
          return false;
        }
        T min_val = entry.Zone().Min();
        T max_val = entry.Zone().Max();
        switch (condition.op) {
          case BinaryOperator::kGreaterThan:
            return !(max_val > constant);
          case BinaryOperator::kGreaterThanOrEqual:
            return !(max_val >= constant);
          case BinaryOperator::kLessThan:
            return !(min_val < constant);
          case BinaryOperator::kLessThanOrEqual:
            return !(min_val <= constant);
          case BinaryOperator::kEqual:
            return constant < min_val || constant > max_val;
          default:
            return false;
        }
      },
      condition.constant);
}

}  // namespace

class SeqScanOperator::Impl {
 public:
  Impl(const gistdb::catalog::TableObject& table, std::vector<std::uint32_t> required_ordinals,
       gistdb::storage::BufferPoolManager& buffer_pool,
       std::optional<ZoneMapSkipCondition> zone_map_skip)
      : table_(table),
        required_ordinals_(std::move(required_ordinals)),
        buffer_pool_(buffer_pool),
        zone_map_skip_(zone_map_skip) {}

  std::optional<DataChunk> GetNext() {
    if (!AdvanceToUsableRowGroup()) {
      return std::nullopt;
    }

    const auto& row_group = table_.RowGroups()[row_group_index_];
    const std::uint32_t row_count = row_group.RowCount();
    const std::uint32_t start_row = vector_index_ * gistdb::kVectorSize;
    const std::uint32_t this_vector_rows =
        std::min<std::uint32_t>(gistdb::kVectorSize, row_count - start_row);

    int_storage_.clear();
    float_storage_.clear();
    varchar_storage_.clear();

    DataChunk chunk(this_vector_rows);
    for (std::uint32_t i = 0; i < this_vector_rows; ++i) {
      chunk.SetRowSelected(i, true);
    }

    for (std::uint32_t ordinal : required_ordinals_) {
      AppendColumn(chunk, row_group, ordinal, start_row, this_vector_rows);
    }

    ++vector_index_;
    if (vector_index_ * gistdb::kVectorSize >= row_count) {
      vector_index_ = 0;
      ++row_group_index_;
    }
    return chunk;
  }

 private:
  [[nodiscard]] bool AdvanceToUsableRowGroup() {
    const auto& row_groups = table_.RowGroups();
    while (row_group_index_ < row_groups.size()) {
      if (zone_map_skip_.has_value() &&
          ZoneMapRulesOutRowGroup(row_groups[row_group_index_], *zone_map_skip_)) {
        ++row_group_index_;
        vector_index_ = 0;
        continue;
      }
      return true;
    }
    return false;
  }

  [[nodiscard]] std::vector<std::byte> FetchWholeRegion(const gistdb::storage::PageRange& range) {
    std::vector<std::byte> scratch(static_cast<std::size_t>(range.page_count) *
                                   gistdb::kPageSizeBytes);
    for (std::uint32_t i = 0; i < range.page_count; ++i) {
      std::byte* page = buffer_pool_.FetchPage(range.start_page_id + i);
      std::memcpy(scratch.data() + (i * gistdb::kPageSizeBytes), page, gistdb::kPageSizeBytes);
      buffer_pool_.UnpinPage(range.start_page_id + i, false);
    }
    return scratch;
  }

  void AppendColumn(DataChunk& chunk, const gistdb::storage::RowGroupFooterEntry& row_group,
                    std::uint32_t ordinal, std::uint32_t start_row, std::uint32_t row_count) {
    gistdb::TypeId type = table_.Column(ordinal).type;
    switch (type) {
      case gistdb::TypeId::kInteger:
        AppendFixedWidth<std::int32_t>(chunk, row_group, ordinal, start_row, row_count,
                                       int_storage_);
        break;
      case gistdb::TypeId::kFloat:
        AppendFixedWidth<float>(chunk, row_group, ordinal, start_row, row_count, float_storage_);
        break;
      case gistdb::TypeId::kVarchar:
        AppendVarchar(chunk, row_group, ordinal, start_row, row_count);
        break;
    }
  }

  template <typename T>
  void AppendFixedWidth(DataChunk& chunk, const gistdb::storage::RowGroupFooterEntry& row_group,
                        std::uint32_t ordinal, std::uint32_t start_row,  // NOLINT
                        std::uint32_t row_count,
                        std::vector<gistdb::storage::FixedWidthColumn<T>>& storage) {
    const auto& entry =
        std::get<gistdb::storage::FixedWidthColumnFooterEntry<T>>(row_group.Column(ordinal));
    const auto& page_range = entry.Pages();
    const std::uint32_t page_id = page_range.start_page_id + vector_index_;

    std::byte* page = buffer_pool_.FetchPage(page_id);
    const auto* values = reinterpret_cast<const T*>(page);

    const auto& bitmap_range = row_group.ValidityBitmapRegion();
    std::vector<std::byte> bitmap_scratch = FetchWholeRegion(bitmap_range);
    const std::byte* bitmap_page = bitmap_scratch.data();

    storage.emplace_back();
    gistdb::storage::FixedWidthColumn<T>& column = storage.back();
    for (std::uint32_t i = 0; i < row_count; ++i) {
      bool is_null = !IsColumnValidAtRow(bitmap_page, row_group.RowCount(), ordinal, start_row + i);
      if (is_null) {
        column.AppendNull();
      } else {
        column.Append(values[i]);
      }
    }

    buffer_pool_.UnpinPage(page_id, false);
    chunk.AddColumn(&column);
  }

  void AppendVarchar(DataChunk& chunk, const gistdb::storage::RowGroupFooterEntry& row_group,
                     std::uint32_t ordinal, std::uint32_t start_row,  // NOLINT
                     std::uint32_t row_count) {
    const auto& entry =
        std::get<gistdb::storage::VarcharColumnFooterEntry>(row_group.Column(ordinal));
    const auto& offsets_range = entry.OffsetsPages();
    const auto& data_range = entry.DataPages();

    std::vector<std::byte> offsets_scratch = FetchWholeRegion(offsets_range);
    const auto* offsets = reinterpret_cast<const std::uint32_t*>(offsets_scratch.data());

    const std::uint32_t byte_start = offsets[start_row];
    const std::uint32_t byte_end = offsets[start_row + row_count];
    const std::uint32_t first_data_page =
        data_range.start_page_id + (byte_start / gistdb::kPageSizeBytes);
    const std::uint32_t last_data_page =
        data_range.start_page_id + (byte_end / gistdb::kPageSizeBytes);

    std::vector<std::byte> scratch((last_data_page - first_data_page + 1) * gistdb::kPageSizeBytes);
    for (std::uint32_t p = first_data_page; p <= last_data_page; ++p) {
      std::byte* page = buffer_pool_.FetchPage(p);
      std::memcpy(scratch.data() + ((p - first_data_page) * gistdb::kPageSizeBytes), page,
                  gistdb::kPageSizeBytes);
      buffer_pool_.UnpinPage(p, false);
    }
    const std::uint32_t scratch_base =
        (first_data_page - data_range.start_page_id) * gistdb::kPageSizeBytes;

    const auto& bitmap_range = row_group.ValidityBitmapRegion();
    std::byte* bitmap_page = buffer_pool_.FetchPage(bitmap_range.start_page_id);

    varchar_storage_.emplace_back();
    gistdb::storage::VarcharColumn& column = varchar_storage_.back();
    for (std::uint32_t i = 0; i < row_count; ++i) {
      bool is_null = !IsColumnValidAtRow(bitmap_page, row_group.RowCount(), ordinal, start_row + i);
      std::uint32_t s = offsets[start_row + i] - scratch_base;
      std::uint32_t e = offsets[start_row + i + 1] - scratch_base;
      if (is_null) {
        column.AppendNull();
      } else {
        std::cerr << "offsets: " << offsets[start_row + i] << " " << offsets[start_row + i + 1]
                  << "\n";
        std::cerr << "scratch_base: " << scratch_base << "\n";
        std::cerr << "s=" << s << " e=" << e << "\n";
        column.Append(std::string_view(reinterpret_cast<const char*>(scratch.data() + s), e - s));
      }
    }

    buffer_pool_.UnpinPage(bitmap_range.start_page_id, false);
    chunk.AddColumn(&column);
  }

  const gistdb::catalog::TableObject& table_;  // NOLINT
  std::vector<std::uint32_t> required_ordinals_;
  gistdb::storage::BufferPoolManager& buffer_pool_;  // NOLINT
  std::optional<ZoneMapSkipCondition> zone_map_skip_;

  std::size_t row_group_index_ = 0;
  std::uint32_t vector_index_ = 0;

  std::vector<gistdb::storage::FixedWidthColumn<std::int32_t>> int_storage_;
  std::vector<gistdb::storage::FixedWidthColumn<float>> float_storage_;
  std::vector<gistdb::storage::VarcharColumn> varchar_storage_;
};

SeqScanOperator::SeqScanOperator(const gistdb::catalog::TableObject& table,
                                 std::vector<std::uint32_t> required_ordinals,
                                 gistdb::storage::BufferPoolManager& buffer_pool,
                                 std::optional<ZoneMapSkipCondition> zone_map_skip)
    : impl_(std::make_unique<Impl>(table, std::move(required_ordinals), buffer_pool,
                                   zone_map_skip)) {}

SeqScanOperator::~SeqScanOperator() = default;
SeqScanOperator::SeqScanOperator(SeqScanOperator&&) noexcept = default;
SeqScanOperator& SeqScanOperator::operator=(SeqScanOperator&&) noexcept = default;

std::optional<DataChunk> SeqScanOperator::GetNext() {
  return impl_->GetNext();
}

}  // namespace gistdb::execution