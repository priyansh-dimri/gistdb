#include "gistdb/execution/insert_executor.hpp"

#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "gistdb/constants.hpp"
#include "gistdb/serialization/byte_io.hpp"
#include "gistdb/storage/column_footer_entry.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/row_group_footer_entry.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {

namespace {

using ColumnVariant =
    std::variant<gistdb::storage::FixedWidthColumn<std::int32_t>,
                 gistdb::storage::FixedWidthColumn<float>, gistdb::storage::VarcharColumn>;

template <typename T>
[[nodiscard]] std::vector<std::uint8_t> SerializeFixedWidth(
    const gistdb::storage::FixedWidthColumn<T>& column, std::uint32_t row_count) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(row_count) * sizeof(T));
  for (std::uint32_t i = 0; i < row_count; ++i) {
    T value = column.GetValue(i);
    std::memcpy(bytes.data() + (i * sizeof(T)), &value, sizeof(T));
  }
  return bytes;
}

[[nodiscard]] std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> SerializeVarchar(
    const gistdb::storage::VarcharColumn& column, std::uint32_t row_count) {
  std::vector<std::uint8_t> offsets_bytes;
  std::vector<std::uint8_t> data_bytes;
  std::uint32_t running_offset = 0;
  gistdb::serialization::WriteU32(offsets_bytes, running_offset);
  for (std::uint32_t i = 0; i < row_count; ++i) {
    std::string value = std::string(column.GetValue(i));
    data_bytes.insert(data_bytes.end(), value.begin(), value.end());
    running_offset += static_cast<std::uint32_t>(value.size());
    gistdb::serialization::WriteU32(offsets_bytes, running_offset);
  }
  return {offsets_bytes, data_bytes};
}

[[nodiscard]] std::vector<std::uint8_t> BuildValidityBitmapBytes(
    const std::vector<ColumnVariant>& columns, std::uint32_t row_count) {
  const std::size_t bytes_per_column = (row_count + 7) / 8;
  std::vector<std::uint8_t> bytes(bytes_per_column * columns.size(), 0);
  for (std::size_t col = 0; col < columns.size(); ++col) {
    std::visit(
        [&](const auto& column) {
          for (std::uint32_t row = 0; row < row_count; ++row) {
            if (!column.IsNull(row)) {
              bytes[(col * bytes_per_column) + (row / 8)] |=
                  static_cast<std::uint8_t>(1U << (row % 8));
            }
          }
        },
        columns[col]);
  }
  return bytes;
}

}  // namespace

class InsertExecutor::Staging {
 public:
  explicit Staging(const gistdb::catalog::TableObject& table) {
    for (std::size_t i = 0; i < table.NumColumns(); ++i) {
      switch (table.Column(i).type) {
        case gistdb::TypeId::kInteger:
          columns_.emplace_back(
              std::in_place_type<gistdb::storage::FixedWidthColumn<std::int32_t>>);
          break;
        case gistdb::TypeId::kFloat:
          columns_.emplace_back(std::in_place_type<gistdb::storage::FixedWidthColumn<float>>);
          break;
        case gistdb::TypeId::kVarchar:
          columns_.emplace_back(std::in_place_type<gistdb::storage::VarcharColumn>);
          break;
      }
    }
  }

  void AppendRow(const std::vector<std::unique_ptr<BoundExpression>>& row) {
    for (std::size_t i = 0; i < row.size(); ++i) {
      const auto* const_node = std::get_if<ConstNode>(&row[i]->node);
      if (const_node == nullptr) {
        throw std::runtime_error("InsertExecutor: INSERT values must be bound literal constants");
      }
      std::visit(
          [&](auto& column) {
            using C = std::decay_t<decltype(column)>;
            std::visit(
                [&](const auto& value) {
                  using V = std::decay_t<decltype(value)>;
                  if constexpr (std::is_same_v<C,
                                               gistdb::storage::FixedWidthColumn<std::int32_t>> &&
                                std::is_same_v<V, std::int32_t>) {  // NOLINT
                    column.Append(value);
                  } else if constexpr (std::is_same_v<C,
                                                      gistdb::storage::FixedWidthColumn<float>> &&
                                       std::is_same_v<V, float>) {
                    column.Append(value);
                  } else if constexpr (std::is_same_v<C, gistdb::storage::VarcharColumn> &&
                                       std::is_same_v<V, std::string>) {
                    column.Append(value);
                  } else {
                    throw std::runtime_error(
                        "InsertExecutor: bound value type doesn't match target column type");
                  }
                },
                const_node->value);
          },
          columns_[i]);
    }
    ++row_count_;
  }

  [[nodiscard]] std::uint32_t RowCount() const { return row_count_; }
  [[nodiscard]] std::vector<ColumnVariant>& Columns() { return columns_; }

 private:
  std::vector<ColumnVariant> columns_;
  std::uint32_t row_count_ = 0;
};

InsertExecutor::InsertExecutor(gistdb::catalog::Catalog& catalog, std::uint32_t table_id)
    : catalog_(catalog),
      table_(*catalog.GetTableById(table_id)),
      disk_manager_(catalog.GetDiskManager()),
      staging_(std::make_unique<Staging>(table_)) {}

InsertExecutor::~InsertExecutor() = default;

void InsertExecutor::InsertRow(const std::vector<std::unique_ptr<BoundExpression>>& row) {
  staging_->AppendRow(row);
  if (staging_->RowCount() >= gistdb::kRowGroupSize) {
    FlushRowGroup();
  }
}

void InsertExecutor::Finish() {
  FlushRowGroup();
}

void InsertExecutor::FlushRowGroup() {
  if (staging_->RowCount() == 0) {
    return;
  }
  const std::uint32_t row_count = staging_->RowCount();

  auto pad_and_write = [&](std::vector<std::uint8_t> bytes) -> gistdb::storage::PageRange {
    std::uint32_t page_count = static_cast<std::uint32_t>(
        (bytes.size() + gistdb::kPageSizeBytes - 1) / gistdb::kPageSizeBytes);
    bytes.resize(static_cast<std::size_t>(page_count) * gistdb::kPageSizeBytes, 0);
    std::uint32_t start_page = disk_manager_.AllocatePages(page_count);
    disk_manager_.WritePages(start_page, bytes);
    return gistdb::storage::PageRange{start_page, page_count};
  };

  std::vector<gistdb::storage::ColumnFooterEntry> column_entries;
  for (auto& col : staging_->Columns()) {
    std::visit(
        [&](auto& column) {
          using C = std::decay_t<decltype(column)>;
          if constexpr (std::is_same_v<C, gistdb::storage::FixedWidthColumn<std::int32_t>>) {
            gistdb::storage::PageRange range =
                pad_and_write(SerializeFixedWidth<std::int32_t>(column, row_count));
            column_entries.push_back(
                gistdb::storage::FixedWidthColumnFooterEntry<std::int32_t>::Build(column, range));
          } else if constexpr (std::is_same_v<C, gistdb::storage::FixedWidthColumn<float>>) {
            gistdb::storage::PageRange range =
                pad_and_write(SerializeFixedWidth<float>(column, row_count));
            column_entries.push_back(
                gistdb::storage::FixedWidthColumnFooterEntry<float>::Build(column, range));
          } else {
            auto [offsets_bytes, data_bytes] = SerializeVarchar(column, row_count);
            gistdb::storage::PageRange offsets_range = pad_and_write(std::move(offsets_bytes));
            gistdb::storage::PageRange data_range = pad_and_write(std::move(data_bytes));
            column_entries.push_back(gistdb::storage::VarcharColumnFooterEntry::Build(
                column, offsets_range, data_range));
          }
        },
        col);
  }

  gistdb::storage::PageRange bitmap_range =
      pad_and_write(BuildValidityBitmapBytes(staging_->Columns(), row_count));

  gistdb::storage::RowGroupFooterEntry row_group(table_.TableId(), row_count, bitmap_range,
                                                 std::move(column_entries));
  catalog_.AddRowGroup(table_.TableName(), std::move(row_group));

  staging_ = std::make_unique<Staging>(table_);
}

}  // namespace gistdb::execution