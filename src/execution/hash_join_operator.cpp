#include "gistdb/execution/hash_join_operator.hpp"

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include "gistdb/constants.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {

namespace {

using ColumnStorage =
    std::variant<gistdb::storage::FixedWidthColumn<std::int32_t>,
                 gistdb::storage::FixedWidthColumn<float>, gistdb::storage::VarcharColumn>;

std::vector<ColumnStorage> MakeEmptyColumns(const std::vector<gistdb::TypeId>& types) {
  std::vector<ColumnStorage> columns;
  for (gistdb::TypeId type : types) {
    switch (type) {
      case gistdb::TypeId::kInteger:
        columns.emplace_back(gistdb::storage::FixedWidthColumn<std::int32_t>{});
        break;
      case gistdb::TypeId::kFloat:
        columns.emplace_back(gistdb::storage::FixedWidthColumn<float>{});
        break;
      case gistdb::TypeId::kVarchar:
        columns.emplace_back(gistdb::storage::VarcharColumn{});
        break;
    }
  }
  return columns;
}

std::vector<ColumnStorage> MakeEmptyColumnsLike(const DataChunk& chunk) {
  std::vector<ColumnStorage> columns;
  for (std::size_t c = 0; c < chunk.NumColumns(); ++c) {
    std::visit(
        [&columns](const auto* col) {
          using ColType = std::decay_t<decltype(*col)>;
          columns.emplace_back(ColType{});
        },
        chunk.Column(static_cast<std::uint32_t>(c)));
  }
  return columns;
}

std::optional<std::string> ComputeJoinKey(const DataChunk& chunk,
                                          const std::vector<std::uint32_t>& key_ordinals,
                                          std::uint32_t row) {
  std::string key;
  for (std::uint32_t ordinal : key_ordinals) {
    bool is_null =
        std::visit([row](const auto* col) { return col->IsNull(row); }, chunk.Column(ordinal));
    if (is_null) {
      return std::nullopt;
    }
    std::visit(
        [&key, row](const auto* col) {
          using ColType = std::decay_t<decltype(*col)>;
          if constexpr (std::is_same_v<ColType, gistdb::storage::VarcharColumn>) {
            std::string_view value = col->GetValue(row);
            auto length = static_cast<std::uint32_t>(value.size());
            key.append(reinterpret_cast<const char*>(&length), sizeof(length));
            key.append(value);
          } else {
            auto value = col->GetValue(row);
            key.append(reinterpret_cast<const char*>(&value), sizeof(value));
          }
        },
        chunk.Column(ordinal));
  }
  return key;
}

void AppendBuildRow(std::vector<ColumnStorage>& build_columns, const DataChunk& chunk,
                    std::uint32_t row) {
  for (std::size_t c = 0; c < build_columns.size(); ++c) {
    std::visit(
        [&chunk, row, c](auto& out_col) {
          using OutType = std::decay_t<decltype(out_col)>;
          std::visit(
              [&out_col, row](const auto* in_col) {
                using InType = std::decay_t<decltype(*in_col)>;
                if constexpr (std::is_same_v<OutType, InType>) {
                  if (in_col->IsNull(row)) {
                    out_col.AppendNull();
                  } else {
                    out_col.Append(in_col->GetValue(row));
                  }
                }
              },
              chunk.Column(static_cast<std::uint32_t>(c)));
        },
        build_columns[c]);
  }
}

void AppendFromColumnStorage(ColumnStorage& out, const ColumnStorage& source, std::uint32_t row) {
  std::visit(
      [&out, row](const auto& s) {
        using SType = std::decay_t<decltype(s)>;
        auto& o = std::get<SType>(out);
        if (s.IsNull(row)) {
          o.AppendNull();
        } else {
          o.Append(s.GetValue(row));
        }
      },
      source);
}

void AppendFromChunkColumn(ColumnStorage& out, const DataChunk& chunk,
                           std::uint32_t ordinal,  // NOLINT
                           std::uint32_t row) {
  std::visit(
      [&out, row](const auto* col) {
        using ColType = std::decay_t<decltype(*col)>;
        auto& o = std::get<ColType>(out);
        if (col->IsNull(row)) {
          o.AppendNull();
        } else {
          o.Append(col->GetValue(row));
        }
      },
      chunk.Column(ordinal));
}

}  // namespace

class HashJoinOperator::Impl {
 public:
  Impl(std::unique_ptr<Operator> build_child, std::unique_ptr<Operator> probe_child,
       std::vector<std::uint32_t> build_key_ordinals, std::vector<std::uint32_t> probe_key_ordinals,
       std::vector<gistdb::TypeId> build_column_types)
      : build_child_(std::move(build_child)),
        probe_child_(std::move(probe_child)),
        build_key_ordinals_(std::move(build_key_ordinals)),
        probe_key_ordinals_(std::move(probe_key_ordinals)),
        build_column_types_(std::move(build_column_types)) {}

  std::optional<DataChunk> GetNext() {
    if (!built_) {
      BuildPhase();
      built_ = true;
    }
    return ProbeNext();
  }

 private:
  void BuildPhase() {
    build_columns_ = MakeEmptyColumns(build_column_types_);
    while (std::optional<DataChunk> chunk = build_child_->GetNext()) {
      for (std::uint32_t i = 0; i < chunk->RowCount(); ++i) {
        if (!chunk->IsRowSelected(i)) {
          continue;
        }
        std::optional<std::string> key = ComputeJoinKey(*chunk, build_key_ordinals_, i);
        if (!key.has_value()) {
          continue;
        }
        auto new_index = static_cast<std::uint32_t>(build_row_count_);
        AppendBuildRow(build_columns_, *chunk, i);
        ++build_row_count_;
        hash_table_[*key].push_back(new_index);
      }
    }
  }

  std::optional<DataChunk> ProbeNext() {  // NOLINT
    if (!current_probe_chunk_.has_value()) {
      current_probe_chunk_ = probe_child_->GetNext();
      if (!current_probe_chunk_.has_value()) {
        return std::nullopt;
      }
      current_probe_row_ = 0;
      current_matches_.clear();
      current_match_index_ = 0;
    }

    DataChunk& probe_chunk = *current_probe_chunk_;
    current_output_build_ = MakeEmptyColumns(build_column_types_);
    current_output_probe_ = MakeEmptyColumnsLike(probe_chunk);
    std::uint32_t emitted = 0;

    while (emitted < gistdb::kVectorSize) {
      while (current_probe_row_ < probe_chunk.RowCount() &&
             current_match_index_ >= current_matches_.size()) {
        if (!probe_chunk.IsRowSelected(current_probe_row_)) {
          ++current_probe_row_;
          continue;
        }
        std::optional<std::string> key =
            ComputeJoinKey(probe_chunk, probe_key_ordinals_, current_probe_row_);
        if (!key.has_value()) {
          ++current_probe_row_;
          continue;
        }
        auto it = hash_table_.find(*key);
        current_matches_ = (it != hash_table_.end()) ? it->second : std::vector<std::uint32_t>{};
        current_match_index_ = 0;
        if (current_matches_.empty()) {
          ++current_probe_row_;
        }
      }

      if (current_probe_row_ >= probe_chunk.RowCount()) {
        current_probe_chunk_.reset();
        break;
      }

      std::uint32_t build_row = current_matches_[current_match_index_];
      for (std::size_t c = 0; c < current_output_build_.size(); ++c) {
        AppendFromColumnStorage(current_output_build_[c], build_columns_[c], build_row);
      }
      for (std::size_t c = 0; c < current_output_probe_.size(); ++c) {
        AppendFromChunkColumn(current_output_probe_[c], probe_chunk, static_cast<std::uint32_t>(c),
                              current_probe_row_);
      }
      ++emitted;
      ++current_match_index_;
      if (current_match_index_ >= current_matches_.size()) {
        ++current_probe_row_;
        current_matches_.clear();
        current_match_index_ = 0;
      }
    }

    DataChunk output(emitted);
    for (auto& col : current_output_build_) {
      std::visit([&output](auto& c) { output.AddColumn(&c); }, col);
    }
    for (auto& col : current_output_probe_) {
      std::visit([&output](auto& c) { output.AddColumn(&c); }, col);
    }
    return output;
  }

  std::unique_ptr<Operator> build_child_;
  std::unique_ptr<Operator> probe_child_;
  std::vector<std::uint32_t> build_key_ordinals_;
  std::vector<std::uint32_t> probe_key_ordinals_;
  std::vector<gistdb::TypeId> build_column_types_;

  bool built_ = false;
  std::vector<ColumnStorage> build_columns_;
  std::size_t build_row_count_ = 0;
  std::unordered_map<std::string, std::vector<std::uint32_t>> hash_table_;

  std::optional<DataChunk> current_probe_chunk_;
  std::uint32_t current_probe_row_ = 0;
  std::vector<std::uint32_t> current_matches_;
  std::size_t current_match_index_ = 0;

  std::vector<ColumnStorage> current_output_build_;
  std::vector<ColumnStorage> current_output_probe_;
};

HashJoinOperator::HashJoinOperator(std::unique_ptr<Operator> build_child,
                                   std::unique_ptr<Operator> probe_child,
                                   std::vector<std::uint32_t> build_key_ordinals,
                                   std::vector<std::uint32_t> probe_key_ordinals,
                                   std::vector<gistdb::TypeId> build_column_types)
    : impl_(std::make_unique<Impl>(std::move(build_child), std::move(probe_child),
                                   std::move(build_key_ordinals), std::move(probe_key_ordinals),
                                   std::move(build_column_types))) {}

HashJoinOperator::HashJoinOperator(HashJoinOperator&&) noexcept = default;
HashJoinOperator& HashJoinOperator::operator=(HashJoinOperator&&) noexcept = default;
HashJoinOperator::~HashJoinOperator() = default;

std::optional<DataChunk> HashJoinOperator::GetNext() {
  return impl_->GetNext();
}

}  // namespace gistdb::execution