# 0005 - Table Object

**Date:** July 11, 2026

## One object holding schema and physical facts together

`TableObject` has a table's columns and its row groups in the same place. We could have kept schema and row group data as two separate look ups that both get keyed by table ID, but then every place that needs "everything about this table" would have to go fetch from two places.

## A name-to-ordinal map is built once in the constructor

Columns are looked up by name a lot, in WHERE clauses, in SELECT lists, and a linear scan through the column list every time would be wasteful for something this cheap to fix. So the constructor builds a `column_ordinal_map_` up front, once, and `FindColumn` is just a fast hash lookup after that.

## Checking table_id on AddRowGroup instead of trusting the caller

`AddRowGroup` checks that the row group's `table_id` actually matches before accepting it, and throws if not. I could have skipped this check, since the code that partitions row groups by table_id should already only ever call this correctly but it is better to fail immediately than corrupt user's data silently.

## Rolling row count as row groups get added

`total_row_count_` updates every time `AddRowGroup` is called, rather than being computed on demand by summing over `row_groups_` because this value is required by the optimizer regularly.
