# 0010 - The Expression Evaluator

**Date:** July 12, 2026

## Computing every row first, masking nulls after

For every expression, arithmetic, comparisons, AND/OR, the evaluator first computes a raw value for every row first, then goes back in a second pass and marks rows null based on the operands validity.

I could have checked validity up front and skipped computing rows that are already going to be null, which would avoid some wasted work. But branching per row to decide whether to compute it would prevent this code from getting vectorized properly, whereas, computing unconditionally and masking afterward stays the same whether it's running as a scalar loop now or as a wide SIMD operation eventually.

## Turning integer divide-by-zero into null instead of undefined behavior

Dividing by zero is undefined behavior for integers in C++. I didn't want undefined behavior sitting in the evaluator, so division computes a safe placeholder value when the divisor is zero, and a second pass explicitly marks that row null regardless of what the placeholder was. Floats don't need this special case, IEEE-754 already turns a zero divisor into infinity or NaN instead of undefined behavior, so only the integer path needed the extra check.
