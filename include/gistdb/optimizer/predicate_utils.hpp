// include/gistdb/optimizer/predicate_utils.hpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gistdb/execution/bound_expression.hpp"

namespace gistdb::optimizer {

using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;

// --- Non-owning inspection (Column Pruning's use case: look at a
// predicate's structure without ever relocating it) -----------------------

// Recursively decomposes `expr` at top-level AND nodes only (Decision 6).
// An OR sub-expression is pushed into `out` whole, as one indivisible
// unit, never split -- splitting an AND is always safe (WHERE a AND b is
// equivalent to filtering on a and b independently, in either order, once
// both are available); splitting an OR is not (a row can satisfy the
// whole predicate via only one branch).
void FlattenConjuncts(const BoundExpression& expr, std::vector<const BoundExpression*>& out);
[[nodiscard]] std::vector<const BoundExpression*> FlattenConjuncts(const BoundExpression& expr);

// --- Owning extraction (Predicate Pushdown's use case: actually relocate
// conjuncts elsewhere in the tree, which requires taking ownership) -------

// Same AND/OR decomposition rule as FlattenConjuncts, but consumes `expr`
// and moves each resulting conjunct out as its own owned subtree. This is
// the version Pushdown needs -- FlattenConjuncts alone can't support
// relocation, since its output is only ever pointers into a tree someone
// else still owns.
void ExtractConjuncts(std::unique_ptr<BoundExpression> expr,
                      std::vector<std::unique_ptr<BoundExpression>>& out);
[[nodiscard]] std::vector<std::unique_ptr<BoundExpression>> ExtractConjuncts(
    std::unique_ptr<BoundExpression> expr);

// Inverse-ish of ExtractConjuncts: ANDs a list of owned conjuncts back
// together, left-associatively, into one expression. Used whenever a
// relocated (or never-moved) set of conjuncts needs to become a single
// LogicalFilter::predicate again. pre: !conjuncts.empty().
[[nodiscard]] std::unique_ptr<BoundExpression> RebuildConjunction(
    std::vector<std::unique_ptr<BoundExpression>> conjuncts);

// --- Column-reference inspection, shared by both rules --------------------

// Every BoundColumnRef reachable within `expr`. Not deduplicated -- every
// call site here treats this as a set via linear membership checks, so
// duplicates cost a little but are never incorrect.
void CollectColumnRefs(const BoundExpression& expr, std::vector<BoundColumnRef>& out);
[[nodiscard]] std::vector<BoundColumnRef> CollectColumnRefs(const BoundExpression& expr);

// True iff every BoundColumnRef in `expr` has a table_id (query-scoped
// binding id, not necessarily the physical Catalog table id -- see the
// Binder work's self-join amendment) present in `available_table_ids`.
// Pushdown's core "can this conjunct live here" test (Decision 7): a
// conjunct may be pushed to sit above a given subtree iff every column it
// references is actually produced by that subtree.
[[nodiscard]] bool AllColumnsAvailable(const BoundExpression& expr,
                                       const std::vector<std::uint32_t>& available_table_ids);

}  // namespace gistdb::optimizer