# 0009 - Bound Expression Types

**Date:** July 11, 2026

## A second type enum, just for what expressions evaluate to

Comparisons and AND/OR/NOT need some boolean type, but `TypeId` only has the three storable types. Since boolean was never supposed to be a real column type in GistDB, so I added a separate `ExpressionType` enum, local to the execution layer, that's `TypeId`'s three values plus `kBoolean`.

## Storing the arithmetic result type instead of computing it

For `+`/`-`/`*`/`/`, what the result type actually is depends on the operand types, `INTEGER + FLOAT` promotes to `FLOAT`, so `BinaryOpNode` just carries an `arithmetic_result_type` field, and whoever builds the node decides what goes there.

## Comparisons and logical ops don't trust a stored field at all

Unlike arithmetic, a comparison or an AND/OR is always boolean no matter what its operands are, so `ResultType()` doesn't read a stored value for those cases, it just returns `kBoolean` directly based on the operator.
