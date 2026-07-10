# 0001 - Column Storage and the Row-Group Footer

**Date:** July 10, 2026

## Nulls as a bitmap, not a sentinel

I went with a separate `ValidityBitmap` (one bit per row) instead of reserving a sentinel value like `INT_MIN` to mean null. Sentinels were cheap but they don't generalize as every type would need its own reserved value, and it is not safe if all value range is used by some column.

Also, a bitmap costs a fixed, small amount of memory per row group and works identically for `int32_t`, `float`, and VARCHAR, so `FixedWidthColumn<T>` and `VarcharColumn` both just hold one alongside their data.

## Zone maps have to know when they're empty

`ZoneMap<T>::Update` does not start tracking min/max values until it has seen a real value (`has_values_`). If I would have initialized min/max values to `{0, 0}`(or some other sentinel values) and let a fully null column's row group report that as its range, a filter like `WHERE age > -5` would be wrong to think that row group contains matches when every value in it is actually NULL. Checking `has_values_` before trusting the zone map will avoid such issues.

## Truncating VARCHAR zone maps to a fixed prefix

For VARCHAR I only store the first 8 bytes of the min/max string, not the full value. Storing full strings would make every column's footer entry a different size depending on what's in it, which turns the footer from a flat array of fixed-size records into something that needs its own length-prefixing just to parse. An 8-byte prefix comparison won't distinguish two strings that share a long common prefix, so it'll occasionally fail to prune a row group it could have skipped, but it can never wrongly skip one, and a fixed-size footer entry is worth that tradeoff.

## Rolling my own binary format for the footer

`Footer::Serialize`/`Deserialize` is a plain byte-tag encoding I wrote in which there is **one byte** ahead of each column entry saying whether it's int, float, or varchar. I could have pulled in something like `Protobuf` to handle this, but the only external dependencies here are `libpg_query` and `GTest`, and a footer with three tag types and a handful of fixed width fields do not need a schema-driven serialization library.

## Giving row groups a table_id and a shared validity region now

`RowGroupFooterEntry` already stores a `table_id`, and a single `validity_bitmap_region` covering every column in the row group instead of one region per column. Nothing reads either of these yet, there's no catalog and no query layer. But multiple tables are going to share one physical file eventually, so a row group needs to say which table it belongs to or there's no way to group them later. And a single 10,240 row bitmap is under 1.3KB. Thus by giving every column its own dedicated page for that would waste most of the page, so packing them into one shared region per row group makes more sense than solving that problem twice.

Every one of these classes has a GTest suite backing it under `tests/storage/`.
