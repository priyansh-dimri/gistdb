#include "gistdb/execution/aggregation_operator.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "gistdb/constants.hpp"
#include "gistdb/execution/aggregate_accumulator.hpp"
#include "gistdb/serialization/byte_io.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {

namespace {

using AccumulatorVariant =
    std::variant<CountStarAccumulator, CountAccumulator, SumIntAccumulator, SumFloatAccumulator,
                 AvgIntAccumulator, AvgFloatAccumulator, MinMaxAccumulator<std::int32_t>,
                 MinMaxAccumulator<float>, MinMaxVarcharAccumulator>;

using GroupKeyValue = std::variant<std::int32_t, float, std::string>;

[[nodiscard]] AccumulatorVariant MakeAccumulator(const AggregateSpec& spec) {
  switch (spec.function) {
    case AggregateFunctionKind::kCountStar:
      return CountStarAccumulator{};
    case AggregateFunctionKind::kCount:
      return CountAccumulator{};
    case AggregateFunctionKind::kSum:
      return spec.argument_type == gistdb::TypeId::kInteger
                 ? AccumulatorVariant{SumIntAccumulator{}}
                 : AccumulatorVariant{SumFloatAccumulator{}};
    case AggregateFunctionKind::kAvg:
      return spec.argument_type == gistdb::TypeId::kInteger
                 ? AccumulatorVariant{AvgIntAccumulator{}}
                 : AccumulatorVariant{AvgFloatAccumulator{}};
    case AggregateFunctionKind::kMin:
    case AggregateFunctionKind::kMax:
      switch (spec.argument_type) {
        case gistdb::TypeId::kInteger:
          return MinMaxAccumulator<std::int32_t>{};
        case gistdb::TypeId::kFloat:
          return MinMaxAccumulator<float>{};
        case gistdb::TypeId::kVarchar:
          return MinMaxVarcharAccumulator{};
      }
  }
  throw std::runtime_error("Unhandled AggregateFunctionKind in MakeAccumulator");
}

// Real FixedWidthColumn<T>/VarcharColumn interface (confirmed against
// fixed_width_column.cpp/varchar_column.cpp, and independently against
// hash_join_operator.cpp's real usage): GetValue(i)/IsNull(i) for reading,
// single-arg Append(value) plus a separate AppendNull() for writing.
// There is no Value(i) and no two-argument Append(value, is_null)
// anywhere in this codebase -- every call site below was corrected.
void SerializeKeyBytes(const ColumnView& col, std::uint32_t row, std::string& out) {
  std::vector<std::uint8_t> bytes;
  std::visit(
      [&](const auto* column) {
        using T = std::decay_t<decltype(*column)>;
        bool is_null = column->IsNull(row);
        gistdb::serialization::WriteU8(bytes, is_null ? 1 : 0);
        if (is_null) {
          return;
        }
        if constexpr (std::is_same_v<T, gistdb::storage::FixedWidthColumn<std::int32_t>>) {
          bytes.push_back(0);
          gistdb::serialization::WriteU32(bytes, static_cast<std::uint32_t>(column->GetValue(row)));
        } else if constexpr (std::is_same_v<T, gistdb::storage::FixedWidthColumn<float>>) {
          bytes.push_back(1);
          gistdb::serialization::WriteFloat(bytes, column->GetValue(row));
        } else {
          bytes.push_back(2);
          gistdb::serialization::WriteString(bytes, std::string(column->GetValue(row)));
        }
      },
      col);
  out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

[[nodiscard]] GroupKeyValue ExtractKeyValue(const ColumnView& col, std::uint32_t row) {
  return std::visit(
      [&](const auto* column) -> GroupKeyValue {
        using T = std::decay_t<decltype(*column)>;
        if constexpr (std::is_same_v<T,
                                     gistdb::storage::FixedWidthColumn<std::int32_t>>) {  // NOLINT
          return column->GetValue(row);
        } else if constexpr (std::is_same_v<T, gistdb::storage::FixedWidthColumn<float>>) {
          return column->GetValue(row);
        } else {
          return std::string(column->GetValue(row));
        }
      },
      col);
}

void AccumulateRow(AccumulatorVariant& acc, const DataChunk& chunk, std::uint32_t row,
                   const AggregateSpec& spec) {
  std::visit(
      [&](auto& accumulator) {
        using A = std::decay_t<decltype(accumulator)>;
        if constexpr (std::is_same_v<A, CountStarAccumulator>) {
          accumulator.Add();
        } else {
          const ColumnView& col = chunk.Column(*spec.argument_column);
          std::visit(
              [&](const auto* column) {
                using T = std::decay_t<decltype(*column)>;
                bool is_null = column->IsNull(row);
                if constexpr (std::is_same_v<A, CountAccumulator>) {
                  accumulator.Add(is_null);
                } else if constexpr (std::is_same_v<A, MinMaxVarcharAccumulator>) {
                  if constexpr (std::is_same_v<T, gistdb::storage::VarcharColumn>) {
                    accumulator.Add(column->GetValue(row), is_null);
                  }
                } else {
                  if constexpr (std::is_same_v<T,
                                               gistdb::storage::FixedWidthColumn<std::int32_t>> ||
                                std::is_same_v<T, gistdb::storage::FixedWidthColumn<float>>) {
                    accumulator.Add(column->GetValue(row), is_null);
                  }
                }
              },
              col);
        }
      },
      acc);
}

struct OutputBuilders {
  std::vector<gistdb::storage::FixedWidthColumn<std::int32_t>> int_cols;
  std::vector<gistdb::storage::FixedWidthColumn<float>> float_cols;
  std::vector<gistdb::storage::VarcharColumn> varchar_cols;

  void Clear() {
    int_cols.clear();
    float_cols.clear();
    varchar_cols.clear();
  }
};

}  // namespace

class AggregationOperator::Impl {
 public:
  Impl(std::unique_ptr<Operator> child, std::vector<std::uint32_t> group_by_columns,
       std::vector<gistdb::TypeId> group_by_types, std::vector<AggregateSpec> aggregates)
      : child_(std::move(child)),
        group_by_columns_(std::move(group_by_columns)),
        group_by_types_(std::move(group_by_types)),
        aggregates_(std::move(aggregates)) {}

  std::optional<DataChunk> GetNext() {
    if (!drained_) {
      Drain();
    }
    if (emit_cursor_ >= group_order_.size()) {
      return std::nullopt;
    }
    return EmitBatch();
  }

 private:
  void Drain() {
    drained_ = true;
    if (group_by_columns_.empty()) {
      const std::string kImplicitKey;
      group_order_.push_back(kImplicitKey);
      display_values_[kImplicitKey] = {};
      auto& accs = accumulators_[kImplicitKey];
      for (const auto& spec : aggregates_) {
        accs.push_back(MakeAccumulator(spec));
      }
    }

    while (std::optional<DataChunk> chunk = child_->GetNext()) {
      for (std::uint32_t row = 0; row < chunk->RowCount(); ++row) {
        if (!chunk->IsRowSelected(row)) {
          continue;
        }

        std::string key;
        std::vector<GroupKeyValue> values;
        for (std::uint32_t col : group_by_columns_) {
          SerializeKeyBytes(chunk->Column(col), row, key);
          values.push_back(ExtractKeyValue(chunk->Column(col), row));
        }
        if (group_by_columns_.empty()) {
          key.clear();
        }

        auto [it, inserted] = accumulators_.try_emplace(key);
        if (inserted) {
          group_order_.push_back(key);
          display_values_[key] = std::move(values);
          for (const auto& spec : aggregates_) {
            it->second.push_back(MakeAccumulator(spec));
          }
        }
        for (std::size_t i = 0; i < aggregates_.size(); ++i) {
          AccumulateRow(it->second[i], *chunk, row, aggregates_[i]);
        }
      }
    }
  }

  [[nodiscard]] DataChunk EmitBatch() {  // NOLINT
    const std::uint32_t batch_size = static_cast<std::uint32_t>(
        std::min<std::size_t>(kVectorSize, group_order_.size() - emit_cursor_));
    DataChunk output(batch_size);

    // builders_ is Impl member state, cleared and rebuilt fresh each call
    // -- NOT a local. `output`'s ColumnViews point directly into it
    // (DataChunk never copies column values), so it must outlive this
    // function call; a local here (the original bug) is destroyed the
    // instant EmitBatch returns, leaving every ColumnView dangling.
    builders_.Clear();

    // column_order_ records (type, index-within-that-type-vector) for
    // each column in true LOGICAL position (group-by columns first, then
    // aggregates, matching LogicalAggregate::output_columns' documented
    // contract). This is necessary because int_cols/float_cols/
    // varchar_cols are three separate typed vectors -- assembling output
    // by walking them one-after-another (the original code) silently
    // groups columns by storage type instead of logical position, e.g.
    // GROUP BY a varchar column while SUMming an int column would emit
    // [sum, category] instead of [category, sum].
    std::vector<std::pair<gistdb::TypeId, std::size_t>> column_order;

    for (gistdb::TypeId type : group_by_types_) {
      if (type == gistdb::TypeId::kInteger) {
        column_order.emplace_back(gistdb::TypeId::kInteger, builders_.int_cols.size());
        builders_.int_cols.emplace_back();
      } else if (type == gistdb::TypeId::kFloat) {
        column_order.emplace_back(gistdb::TypeId::kFloat, builders_.float_cols.size());
        builders_.float_cols.emplace_back();
      } else {
        column_order.emplace_back(gistdb::TypeId::kVarchar, builders_.varchar_cols.size());
        builders_.varchar_cols.emplace_back();
      }
    }
    for (const auto& spec : aggregates_) {
      gistdb::TypeId out_type =
          spec.function == AggregateFunctionKind::kAvg
              ? gistdb::TypeId::kFloat
              : (spec.function == AggregateFunctionKind::kCountStar ||  // NOLINT
                         spec.function == AggregateFunctionKind::kCount
                     ? gistdb::TypeId::kInteger
                     : spec.argument_type);
      if (out_type == gistdb::TypeId::kInteger) {
        column_order.emplace_back(gistdb::TypeId::kInteger, builders_.int_cols.size());
        builders_.int_cols.emplace_back();
      } else if (out_type == gistdb::TypeId::kFloat) {
        column_order.emplace_back(gistdb::TypeId::kFloat, builders_.float_cols.size());
        builders_.float_cols.emplace_back();
      } else {
        column_order.emplace_back(gistdb::TypeId::kVarchar, builders_.varchar_cols.size());
        builders_.varchar_cols.emplace_back();
      }
    }

    for (std::uint32_t i = 0; i < batch_size; ++i) {
      const std::string& key = group_order_[emit_cursor_ + i];
      std::size_t int_idx = 0;
      std::size_t float_idx = 0;
      std::size_t varchar_idx = 0;

      const auto& values = display_values_[key];
      for (std::size_t g = 0; g < group_by_types_.size(); ++g) {
        std::visit(
            [&](const auto& v) {
              using V = std::decay_t<decltype(v)>;
              if constexpr (std::is_same_v<V, std::int32_t>) {
                builders_.int_cols[int_idx++].Append(v);
              } else if constexpr (std::is_same_v<V, float>) {
                builders_.float_cols[float_idx++].Append(v);
              } else {
                builders_.varchar_cols[varchar_idx++].Append(v);
              }
            },
            values[g]);
      }

      const auto& accs = accumulators_[key];
      for (std::size_t a = 0; a < aggregates_.size(); ++a) {
        const AggregateSpec& spec = aggregates_[a];
        std::visit(
            [&](const auto& accumulator) {  // NOLINT
              using A = std::decay_t<decltype(accumulator)>;
              if constexpr (std::is_same_v<A, CountStarAccumulator> ||
                            std::is_same_v<A, CountAccumulator>) {
                builders_.int_cols[int_idx++].Append(
                    static_cast<std::int32_t>(accumulator.Count()));
              } else if constexpr (std::is_same_v<A, SumIntAccumulator>) {
                auto& out_col = builders_.int_cols[int_idx++];
                if (accumulator.HasValues()) {
                  out_col.Append(static_cast<std::int32_t>(accumulator.Sum()));
                } else {
                  out_col.AppendNull();
                }
              } else if constexpr (std::is_same_v<A, SumFloatAccumulator>) {
                auto& out_col = builders_.float_cols[float_idx++];
                if (accumulator.HasValues()) {
                  out_col.Append(static_cast<float>(accumulator.Sum()));
                } else {
                  out_col.AppendNull();
                }
              } else if constexpr (std::is_same_v<A, AvgIntAccumulator> ||
                                   std::is_same_v<A, AvgFloatAccumulator>) {
                auto& out_col = builders_.float_cols[float_idx++];
                if (accumulator.HasValues()) {
                  out_col.Append(static_cast<float>(accumulator.Average()));
                } else {
                  out_col.AppendNull();
                }
              } else if constexpr (std::is_same_v<A, MinMaxVarcharAccumulator>) {
                auto& out_col = builders_.varchar_cols[varchar_idx++];
                if (!accumulator.HasValues()) {
                  out_col.AppendNull();
                } else if (spec.function == AggregateFunctionKind::kMax) {
                  out_col.Append(accumulator.Max());
                } else {
                  out_col.Append(accumulator.Min());
                }
              } else if constexpr (std::is_same_v<A, MinMaxAccumulator<std::int32_t>>) {
                auto& out_col = builders_.int_cols[int_idx++];
                if (!accumulator.HasValues()) {
                  out_col.AppendNull();
                } else if (spec.function == AggregateFunctionKind::kMax) {
                  out_col.Append(accumulator.Max());
                } else {
                  out_col.Append(accumulator.Min());
                }
              } else {  // MinMaxAccumulator<float>
                auto& out_col = builders_.float_cols[float_idx++];
                if (!accumulator.HasValues()) {
                  out_col.AppendNull();
                } else if (spec.function == AggregateFunctionKind::kMax) {
                  out_col.Append(accumulator.Max());
                } else {
                  out_col.Append(accumulator.Min());
                }
              }
            },
            accs[a]);
      }
    }

    for (const auto& [type, index] : column_order) {
      if (type == gistdb::TypeId::kInteger) {
        output.AddColumn(&builders_.int_cols[index]);
      } else if (type == gistdb::TypeId::kFloat) {
        output.AddColumn(&builders_.float_cols[index]);
      } else {
        output.AddColumn(&builders_.varchar_cols[index]);
      }
    }

    emit_cursor_ += batch_size;
    return output;
  }

  std::unique_ptr<Operator> child_;
  std::vector<std::uint32_t> group_by_columns_;
  std::vector<gistdb::TypeId> group_by_types_;
  std::vector<AggregateSpec> aggregates_;

  bool drained_ = false;
  std::vector<std::string> group_order_;
  std::unordered_map<std::string, std::vector<GroupKeyValue>> display_values_;
  std::unordered_map<std::string, std::vector<AccumulatorVariant>> accumulators_;
  std::size_t emit_cursor_ = 0;
  OutputBuilders builders_;
};

AggregationOperator::AggregationOperator(std::unique_ptr<Operator> child,
                                         std::vector<std::uint32_t> group_by_columns,
                                         std::vector<gistdb::TypeId> group_by_types,
                                         std::vector<AggregateSpec> aggregates)
    : impl_(std::make_unique<Impl>(std::move(child), std::move(group_by_columns),
                                   std::move(group_by_types), std::move(aggregates))) {}

AggregationOperator::~AggregationOperator() = default;
AggregationOperator::AggregationOperator(AggregationOperator&&) noexcept = default;
AggregationOperator& AggregationOperator::operator=(AggregationOperator&&) noexcept = default;

std::optional<DataChunk> AggregationOperator::GetNext() {
  return impl_->GetNext();
}

}  // namespace gistdb::execution