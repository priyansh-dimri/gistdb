# 0012 - Aggregate Accumulators

**Date:** July 12, 2026

## Widening SUM's accumulator, not its storage

`SumIntAccumulator` and `SumFloatAccumulator` add into an `int64_t`/`double` internally even though the column itself is `int32_t`/`float`. The column stays narrow on disk because that's what keeps it cache-dense and SIMD-friendly.

## AVG holds sum and count separately, and never divides early

`AvgIntAccumulator`/`AvgFloatAccumulator` track a running sum and a running count as two separate numbers, and only divide when `Average()` is actually called. I could divide incrementally as each value comes in, but then the count used for one row's division isn't the true final count for the group, since more rows may still be coming.
