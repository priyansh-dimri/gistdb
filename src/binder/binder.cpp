#include "gistdb/binder/binder.hpp"

#include <algorithm>
#include <numeric>
#include <type_traits>
#include <utility>

#include "gistdb/binder/resolution.hpp"
#include "gistdb/binder/type_coercion.hpp"

namespace gistdb::binder {

namespace {

using gistdb::execution::BinaryOperator;
using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
using gistdb::execution::ExpressionType;

void FlattenAnd(const RawExpression& expr, std::vector<const RawExpression*>& out) {
  if (const auto* bin = std::get_if<BinaryOpNode>(&expr.node);
      bin != nullptr && bin->op == BinaryOperator::kAnd) {
    FlattenAnd(*bin->left, out);
    FlattenAnd(*bin->right, out);
  } else {
    out.push_back(&expr);
  }
}

[[nodiscard]] std::unique_ptr<BoundExpression> BindScalarExpression(const RawExpression& raw,
                                                                    const ResolutionScope& scope) {
  return std::visit(
      [&](const auto& node) -> std::unique_ptr<BoundExpression> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ColumnRefNode>) {
          BoundColumnRef ref = scope.Resolve(node);
          return gistdb::execution::MakeColumnRef(ref.table_id, ref.ordinal, ref.type);
        } else if constexpr (std::is_same_v<T, ConstNode>) {
          return BindConst(node);
        } else if constexpr (std::is_same_v<T, BinaryOpNode>) {
          auto left = BindScalarExpression(*node.left, scope);
          auto right = BindScalarExpression(*node.right, scope);
          ExpressionType result_type =
              ResultTypeForBinaryOp(node.op, left->ResultType(), right->ResultType());
          if (IsLogicalOperator(node.op)) {
            return gistdb::execution::MakeBooleanOp(node.op, std::move(left), std::move(right));
          }
          return gistdb::execution::MakeArithmeticOp(node.op, std::move(left), std::move(right),
                                                     result_type);
        } else if constexpr (std::is_same_v<T, UnaryOpNode>) {
          auto operand = BindScalarExpression(*node.operand, scope);
          (void)ResultTypeForUnaryOp(node.op, operand->ResultType());
          return gistdb::execution::MakeNot(std::move(operand));
        } else {
          throw BindException(
              "Aggregate functions may only appear as a bare SELECT-list item, not nested "
              "inside a larger expression");
        }
      },
      raw.node);
}

[[nodiscard]] std::unique_ptr<BoundExpression> BindPredicate(const RawExpression& raw,
                                                             const ResolutionScope& scope) {
  auto bound = BindScalarExpression(raw, scope);
  if (bound->ResultType() != ExpressionType::kBoolean) {
    throw BindException("Expected a boolean expression here");
  }
  return bound;
}

void CollectColumnRefs(const BoundExpression& expr, std::vector<BoundColumnRef>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, gistdb::execution::BoundColumnRef>) {
          out.push_back(node);
        } else if constexpr (std::is_same_v<T, gistdb::execution::BinaryOpNode>) {
          CollectColumnRefs(*node.left, out);
          CollectColumnRefs(*node.right, out);
        } else if constexpr (std::is_same_v<T, gistdb::execution::UnaryOpNode>) {
          CollectColumnRefs(*node.operand, out);
        }
      },
      expr.node);
}

[[nodiscard]] bool ContainsColumn(const std::vector<BoundColumnRef>& haystack,
                                  const BoundColumnRef& needle) {
  return std::any_of(haystack.begin(), haystack.end(), [&](const BoundColumnRef& c) {
    return c.table_id == needle.table_id && c.ordinal == needle.ordinal;
  });
}

[[nodiscard]] ExpressionType ResultTypeForAggregate(AggregateFunctionKind kind,
                                                    std::optional<ExpressionType> argument_type) {
  switch (kind) {
    case AggregateFunctionKind::kCountStar:
    case AggregateFunctionKind::kCount:
      return ExpressionType::kInteger;
    case AggregateFunctionKind::kAvg:
      return ExpressionType::kFloat;
    case AggregateFunctionKind::kSum:
    case AggregateFunctionKind::kMin:
    case AggregateFunctionKind::kMax:
      if (!argument_type.has_value()) {
        throw BindException("Aggregate function requires an argument type");
      }
      return *argument_type;
  }
  throw BindException("Unhandled aggregate function kind");
}

[[nodiscard]] gistdb::TypeId AggregateOutputTypeId(AggregateFunctionKind kind,
                                                   std::optional<gistdb::TypeId> argument_type) {
  switch (kind) {
    case AggregateFunctionKind::kCountStar:
    case AggregateFunctionKind::kCount:
      return gistdb::TypeId::kInteger;
    case AggregateFunctionKind::kAvg:
      return gistdb::TypeId::kFloat;
    case AggregateFunctionKind::kSum:
    case AggregateFunctionKind::kMin:
    case AggregateFunctionKind::kMax:
      return *argument_type;
  }
  throw BindException("Unhandled aggregate function kind");
}

[[nodiscard]] AggregateCall BindAggregateCall(const FunctionCallNode& call,
                                              const ResolutionScope& scope) {
  std::string name = call.name;
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);

  if (name == "count" && call.is_star_arg) {
    return AggregateCall{AggregateFunctionKind::kCountStar, std::nullopt};
  }
  if (call.args.size() != 1) {
    throw BindException("Aggregate function '" + name + "' expects exactly one argument");
  }

  const auto* column_ref = std::get_if<ColumnRefNode>(&call.args[0]->node);
  if (column_ref == nullptr) {
    throw BindException("Aggregate function arguments must be a plain column reference");
  }
  BoundColumnRef ref = scope.Resolve(*column_ref);

  AggregateFunctionKind kind;  // NOLINT
  if (name == "count") {
    kind = AggregateFunctionKind::kCount;
  } else if (name == "sum") {
    kind = AggregateFunctionKind::kSum;
    if (ref.type == gistdb::TypeId::kVarchar) {
      throw BindException("SUM does not support VARCHAR");
    }
  } else if (name == "avg") {
    kind = AggregateFunctionKind::kAvg;
    if (ref.type == gistdb::TypeId::kVarchar) {
      throw BindException("AVG does not support VARCHAR");
    }
  } else if (name == "min") {
    kind = AggregateFunctionKind::kMin;
  } else if (name == "max") {
    kind = AggregateFunctionKind::kMax;
  } else {
    throw BindException("Unsupported function: " + name);
  }
  return AggregateCall{.function = kind, .argument = ref};
}

class DisjointSet {
 public:
  explicit DisjointSet(std::size_t n) : parent_(n) { std::iota(parent_.begin(), parent_.end(), 0); }
  std::size_t Find(std::size_t x) {
    while (parent_[x] != x) {
      parent_[x] = parent_[parent_[x]];
      x = parent_[x];
    }
    return x;
  }
  void Union(std::size_t a, std::size_t b) { parent_[Find(a)] = Find(b); }

 private:
  std::vector<std::size_t> parent_;
};

[[nodiscard]] gistdb::TypeId MapRawTypeName(const std::string& raw_type_name) {
  if (raw_type_name == "int4") {
    return gistdb::TypeId::kInteger;
  }
  if (raw_type_name == "float4") {
    return gistdb::TypeId::kFloat;
  }
  if (raw_type_name == "varchar" || raw_type_name == "text") {
    return gistdb::TypeId::kVarchar;
  }
  throw BindException("Unsupported column type: " + raw_type_name +
                      " (GistDB supports only INTEGER/int4, FLOAT/float4, VARCHAR/text)");
}

struct FromBinding {
  std::unique_ptr<LogicalPlanNode> plan;
  std::vector<std::uint32_t> binding_ids;
};

[[nodiscard]] FromBinding BindTableRef(const TableRefNode& ref, ResolutionScope& scope,  // NOLINT
                                       const gistdb::catalog::Catalog& catalog) {
  return std::visit(
      [&](const auto& node) -> FromBinding {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, BaseTableRefNode>) {
          std::uint32_t binding_id = scope.RegisterTable(node.table_name, node.alias, catalog);
          const ResolvedTableBinding& binding = scope.BindingFor(binding_id);
          std::vector<OutputColumn> output_columns;
          for (std::size_t i = 0; i < binding.table->NumColumns(); ++i) {
            const auto& col = binding.table->Column(i);
            output_columns.push_back(
                OutputColumn{col.name, gistdb::execution::ToExpressionType(col.type)});
          }
          auto plan =
              MakeLogicalScan(binding_id, binding.table->TableId(), std::move(output_columns));
          return FromBinding{std::move(plan), {binding_id}};
        } else {
          if (node.left == nullptr || node.right == nullptr) {
            throw BindException("Malformed JOIN");
          }
          FromBinding left = BindTableRef(*node.left, scope, catalog);
          FromBinding right = BindTableRef(*node.right, scope, catalog);

          std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi_conditions;
          if (node.on_condition != nullptr) {
            std::vector<const RawExpression*> conjuncts;
            FlattenAnd(*node.on_condition, conjuncts);
            for (const RawExpression* conjunct : conjuncts) {
              const auto* bin = std::get_if<BinaryOpNode>(&conjunct->node);
              const ColumnRefNode* lhs =
                  bin != nullptr ? std::get_if<ColumnRefNode>(&bin->left->node) : nullptr;
              const ColumnRefNode* rhs =
                  bin != nullptr ? std::get_if<ColumnRefNode>(&bin->right->node) : nullptr;
              if (bin == nullptr || bin->op != BinaryOperator::kEqual || lhs == nullptr ||
                  rhs == nullptr) {
                throw BindException(
                    "JOIN condition must be an equality (or AND of equalities) between plain "
                    "column references (GistDB only supports a Hash Join operator)");
              }
              equi_conditions.emplace_back(scope.Resolve(*lhs), scope.Resolve(*rhs));
            }
          }
          std::vector<std::uint32_t> ids = left.binding_ids;
          ids.insert(ids.end(), right.binding_ids.begin(), right.binding_ids.end());
          auto plan = MakeLogicalJoin(std::move(left.plan), std::move(right.plan),
                                      std::move(equi_conditions));
          return FromBinding{std::move(plan), std::move(ids)};
        }
      },
      ref.node);
}

struct FromClauseResult {
  std::unique_ptr<LogicalPlanNode> plan;
  std::vector<const RawExpression*> remaining_where_conjuncts;
};

[[nodiscard]] FromClauseResult BindFromClause(  // NOLINT
    const std::vector<std::unique_ptr<TableRefNode>>& items,
    const std::vector<const RawExpression*>& where_conjuncts, ResolutionScope& scope,
    const gistdb::catalog::Catalog& catalog) {
  if (items.empty()) {
    throw BindException("FROM clause must reference at least one table");
  }
  std::vector<FromBinding> parts;
  parts.reserve(items.size());
  for (const auto& item : items) {
    parts.push_back(BindTableRef(*item, scope, catalog));
  }
  if (parts.size() == 1) {
    return FromClauseResult{.plan = std::move(parts[0].plan),
                            .remaining_where_conjuncts = where_conjuncts};
  }

  DisjointSet groups(parts.size());
  auto part_index_of = [&](std::uint32_t binding_id) -> std::size_t {
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (std::find(parts[i].binding_ids.begin(), parts[i].binding_ids.end(), binding_id) !=
          parts[i].binding_ids.end()) {
        return i;
      }
    }
    throw BindException("Internal error: binding_id not found in any FROM part");
  };

  std::vector<const RawExpression*> remaining;
  std::vector<std::pair<std::size_t, std::size_t>> connecting_pairs;
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> connecting_equalities;

  for (const RawExpression* conjunct : where_conjuncts) {
    const auto* bin = std::get_if<BinaryOpNode>(&conjunct->node);
    const ColumnRefNode* lhs =
        bin != nullptr ? std::get_if<ColumnRefNode>(&bin->left->node) : nullptr;
    const ColumnRefNode* rhs =
        bin != nullptr ? std::get_if<ColumnRefNode>(&bin->right->node) : nullptr;
    if (bin != nullptr && bin->op == BinaryOperator::kEqual && lhs != nullptr && rhs != nullptr) {
      BoundColumnRef left_ref = scope.Resolve(*lhs);
      BoundColumnRef right_ref = scope.Resolve(*rhs);
      std::size_t a = part_index_of(left_ref.table_id);
      std::size_t b = part_index_of(right_ref.table_id);
      if (a != b) {
        connecting_pairs.emplace_back(a, b);
        connecting_equalities.emplace_back(left_ref, right_ref);
        continue;
      }
    }
    remaining.push_back(conjunct);
  }

  std::unique_ptr<LogicalPlanNode> combined = std::move(parts[0].plan);
  std::size_t combined_root = 0;
  for (std::size_t i = 1; i < parts.size(); ++i) {
    bool connected = false;
    for (std::size_t k = 0; k < connecting_pairs.size(); ++k) {
      auto [a, b] = connecting_pairs[k];
      if ((groups.Find(a) == groups.Find(combined_root) && b == i) ||
          (groups.Find(b) == groups.Find(combined_root) && a == i)) {
        std::vector<std::pair<BoundColumnRef, BoundColumnRef>> eq{connecting_equalities[k]};
        combined = MakeLogicalJoin(std::move(combined), std::move(parts[i].plan), std::move(eq));
        groups.Union(i, combined_root);
        connected = true;
        break;
      }
    }
    if (!connected) {
      throw BindException(
          "Cartesian product not allowed: FROM items must be connected by a WHERE-clause");
    }
  }
  return FromClauseResult{.plan = std::move(combined), .remaining_where_conjuncts = remaining};
}

[[nodiscard]] std::unique_ptr<LogicalPlanNode> BindSelect(const SelectNode& select,  // NOLINT
                                                          const gistdb::catalog::Catalog& catalog) {
  if (select.has_distinct || select.has_order_by || select.has_limit || select.has_with_clause ||
      select.has_set_operation) {
    throw BindException(
        "DISTINCT, ORDER BY, LIMIT/OFFSET, WITH, and set operations are not supported ");
  }

  ResolutionScope scope;
  std::vector<const RawExpression*> where_conjuncts;
  if (select.where_clause != nullptr) {
    FlattenAnd(*select.where_clause, where_conjuncts);
  }
  FromClauseResult from = BindFromClause(select.from_tables, where_conjuncts, scope, catalog);

  std::unique_ptr<LogicalPlanNode> plan = std::move(from.plan);
  if (!from.remaining_where_conjuncts.empty()) {
    std::unique_ptr<BoundExpression> predicate;
    for (const RawExpression* conjunct : from.remaining_where_conjuncts) {
      auto bound = BindPredicate(*conjunct, scope);
      predicate = predicate == nullptr
                      ? std::move(bound)
                      : gistdb::execution::MakeBooleanOp(BinaryOperator::kAnd, std::move(predicate),
                                                         std::move(bound));
    }
    plan = MakeLogicalFilter(std::move(plan), std::move(predicate));
  }

  std::vector<std::pair<std::size_t, const FunctionCallNode*>> aggregate_items;
  for (std::size_t i = 0; i < select.select_list.size(); ++i) {
    const SelectItem& item = select.select_list[i];
    if (!item.is_wildcard) {
      if (const auto* call = std::get_if<FunctionCallNode>(&item.expression->node)) {
        aggregate_items.emplace_back(i, call);
      }
    }
  }

  const bool is_aggregate_query =
      !aggregate_items.empty() || !select.group_by.empty() || select.having_clause != nullptr;

  std::vector<BoundColumnRef> group_by_cols;
  std::vector<AggregateCall> aggregates;
  if (is_aggregate_query) {
    if (select.select_list.empty() ||
        std::any_of(select.select_list.begin(), select.select_list.end(),
                    [](const SelectItem& item) { return item.is_wildcard; })) {
      throw BindException("SELECT * cannot be combined with GROUP BY or aggregate functions");
    }

    std::vector<std::string> group_by_names;
    for (const auto& group_expr : select.group_by) {
      const auto* column_ref = std::get_if<ColumnRefNode>(&group_expr->node);
      if (column_ref == nullptr) {
        throw BindException("GROUP BY currently only supports plain column references");
      }
      group_by_cols.push_back(scope.Resolve(*column_ref));
      group_by_names.push_back(column_ref->column_name);
    }

    std::vector<OutputColumn> output_columns;
    for (std::size_t g = 0; g < group_by_cols.size(); ++g) {
      output_columns.push_back(  // NOLINT
          OutputColumn{group_by_names[g],
                       gistdb::execution::ToExpressionType(group_by_cols[g].type)});
    }
    for (const auto& [index, call] : aggregate_items) {
      AggregateCall agg = BindAggregateCall(*call, scope);
      ExpressionType result_type = ResultTypeForAggregate(
          agg.function, agg.argument.has_value()
                            ? std::optional(gistdb::execution::ToExpressionType(agg.argument->type))
                            : std::nullopt);
      aggregates.push_back(agg);
      output_columns.push_back(OutputColumn{call->name, result_type});
    }

    for (std::size_t i = 0; i < select.select_list.size(); ++i) {
      bool is_aggregate = std::any_of(aggregate_items.begin(), aggregate_items.end(),
                                      [i](const auto& p) { return p.first == i; });
      if (!is_aggregate) {
        auto bound = BindScalarExpression(*select.select_list[i].expression, scope);
        std::vector<BoundColumnRef> referenced;
        CollectColumnRefs(*bound, referenced);
        for (const auto& ref : referenced) {
          if (!ContainsColumn(group_by_cols, ref)) {
            throw BindException("Non-aggregated SELECT column must appear in GROUP BY");
          }
        }
      }
    }

    plan =
        MakeLogicalAggregate(std::move(plan), group_by_cols, aggregates, std::move(output_columns));

    if (select.having_clause != nullptr) {
      plan = MakeLogicalFilter(std::move(plan), BindPredicate(*select.having_clause, scope));
    }
  }

  std::vector<std::unique_ptr<BoundExpression>> select_expressions;
  std::vector<OutputColumn> output_columns;
  if (is_aggregate_query) {
    std::size_t next_aggregate_index = 0;
    for (std::size_t i = 0; i < select.select_list.size(); ++i) {
      bool item_is_aggregate = std::any_of(aggregate_items.begin(), aggregate_items.end(),
                                           [i](const auto& p) { return p.first == i; });
      if (item_is_aggregate) {
        auto position = static_cast<std::uint32_t>(group_by_cols.size() + next_aggregate_index);
        const AggregateCall& agg = aggregates[next_aggregate_index];
        gistdb::TypeId result_type = AggregateOutputTypeId(
            agg.function,
            agg.argument.has_value() ? std::optional(agg.argument->type) : std::nullopt);
        select_expressions.push_back(
            gistdb::execution::MakeColumnRef(kAggregateOutputBindingId, position, result_type));
        output_columns.push_back(OutputColumn{aggregate_items[next_aggregate_index].second->name,
                                              gistdb::execution::ToExpressionType(result_type)});
        ++next_aggregate_index;
      } else {
        const auto* column_ref =
            std::get_if<ColumnRefNode>(&select.select_list[i].expression->node);
        BoundColumnRef resolved = scope.Resolve(*column_ref);
        auto it = std::find_if(
            group_by_cols.begin(), group_by_cols.end(), [&](const BoundColumnRef& key) {
              return key.table_id == resolved.table_id && key.ordinal == resolved.ordinal;
            });
        auto position = static_cast<std::uint32_t>(std::distance(group_by_cols.begin(), it));
        select_expressions.push_back(
            gistdb::execution::MakeColumnRef(kAggregateOutputBindingId, position, resolved.type));
        output_columns.push_back(OutputColumn{column_ref->column_name,
                                              gistdb::execution::ToExpressionType(resolved.type)});
      }
    }
    return MakeLogicalProjection(std::move(plan), std::move(select_expressions),
                                 std::move(output_columns));
  }
  for (const SelectItem& item : select.select_list) {
    if (item.is_wildcard) {
      for (const auto& binding : scope.Bindings()) {
        for (std::size_t i = 0; i < binding.table->NumColumns(); ++i) {
          const auto& col = binding.table->Column(i);
          select_expressions.push_back(
              gistdb::execution::MakeColumnRef(binding.binding_id, col.ordinal, col.type));
          output_columns.push_back(
              OutputColumn{col.name, gistdb::execution::ToExpressionType(col.type)});
        }
      }
    } else {
      auto bound = BindScalarExpression(*item.expression, scope);
      ExpressionType type = bound->ResultType();
      std::string display_name;
      if (const auto* col_ref = std::get_if<ColumnRefNode>(&item.expression->node)) {
        display_name = col_ref->column_name;
      }
      select_expressions.push_back(std::move(bound));
      output_columns.push_back(OutputColumn{display_name, type});
    }
  }
  return MakeLogicalProjection(std::move(plan), std::move(select_expressions),
                               std::move(output_columns));
}

[[nodiscard]] BoundInsert BindInsert(const InsertNode& insert,
                                     const gistdb::catalog::Catalog& catalog) {
  const gistdb::catalog::TableObject* table = catalog.GetTable(insert.table_name);
  if (table == nullptr) {
    throw BindException("Unknown table: " + insert.table_name);
  }

  std::vector<std::uint32_t> target_ordinals;
  if (insert.columns.empty()) {
    for (std::uint32_t i = 0; i < table->NumColumns(); ++i) {
      target_ordinals.push_back(i);
    }
  } else {
    for (const auto& name : insert.columns) {
      const gistdb::catalog::ColumnDef* col = table->FindColumn(name);
      if (col == nullptr) {
        throw BindException("Unknown column '" + name + "' in table " + insert.table_name);
      }
      target_ordinals.push_back(col->ordinal);
    }
  }

  BoundInsert result{table->TableId(), {}};
  for (const auto& raw_row : insert.value_rows) {
    if (raw_row.size() != target_ordinals.size()) {
      throw BindException("VALUES row has a different number of entries than the column list");
    }
    if (target_ordinals.size() != table->NumColumns()) {
      throw BindException(
          "Partial column-list INSERT (omitted columns implicitly NULL) is not yet supported -- "
          "blocked on ConstNode's missing null case, see this module's prior flagged gap");
    }
    std::vector<std::unique_ptr<BoundExpression>> bound_row(table->NumColumns());
    for (std::size_t i = 0; i < raw_row.size(); ++i) {
      const auto* const_node = std::get_if<ConstNode>(&raw_row[i]->node);
      if (const_node == nullptr) {
        throw BindException("INSERT values must be literal constants");
      }
      auto bound_value = BindConst(*const_node);
      const gistdb::TypeId expected = table->Column(target_ordinals[i]).type;
      if (gistdb::execution::ToExpressionType(expected) != bound_value->ResultType()) {
        throw BindException("Type mismatch in INSERT value for column " +
                            table->Column(target_ordinals[i]).name);
      }
      bound_row[target_ordinals[i]] = std::move(bound_value);
    }
    result.rows.push_back(std::move(bound_row));
  }
  return result;
}

[[nodiscard]] TableCreated BindCreateTable(const CreateTableNode& create,
                                           gistdb::catalog::Catalog& catalog) {
  std::vector<gistdb::catalog::ColumnDef> columns;
  for (std::uint32_t i = 0; i < create.columns.size(); ++i) {
    const RawColumnDef& raw = create.columns[i];
    columns.push_back(gistdb::catalog::ColumnDef{raw.name, MapRawTypeName(raw.raw_type_name), i});
  }
  std::uint32_t table_id = catalog.CreateTable(create.table_name, std::move(columns));
  return TableCreated{table_id};
}

}  // namespace

BindResult Binder::Bind(const ParsedStatement& statement, gistdb::catalog::Catalog& catalog) {
  try {
    return std::visit(
        [&](const auto& stmt) -> BindResult {
          using T = std::decay_t<decltype(stmt)>;
          if constexpr (std::is_same_v<T, std::unique_ptr<SelectNode>>) {
            return BindSelect(*stmt, catalog);
          } else if constexpr (std::is_same_v<T, std::unique_ptr<InsertNode>>) {
            return BindInsert(*stmt, catalog);
          } else {
            return BindCreateTable(*stmt, catalog);
          }
        },
        statement);
  } catch (const ResolutionException& e) {
    throw BindException(e.what());
  } catch (const TypeCoercionException& e) {
    throw BindException(e.what());
  } catch (const ParseException& e) {
    throw BindException(e.what());
  }
}

}  // namespace gistdb::binder