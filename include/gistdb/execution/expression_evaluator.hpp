#pragma once

#include <variant>

#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"
#include "gistdb/storage/validity_bitmap.hpp"
#include "gistdb/storage/varchar_column.hpp"

namespace gistdb::execution {

// A comparison, logical(AND/OR), or NOT expression's result
struct BooleanResult {
  gistdb::storage::ValidityBitmap values;    // 1 = true
  gistdb::storage::ValidityBitmap validity;  // 1 = non-null
};

// The evaluator's output
using EvaluationResult = std::variant<gistdb::storage::FixedWidthColumn<std::int32_t>,
                                      gistdb::storage::FixedWidthColumn<float>,
                                      gistdb::storage::VarcharColumn, BooleanResult>;

class ExpressionEvaluator {
 public:
  [[nodiscard]] static EvaluationResult Evaluate(const BoundExpression& expr,
                                                 const DataChunk& chunk);
};

}  // namespace gistdb::execution