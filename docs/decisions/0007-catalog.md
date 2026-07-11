# 0007 - The Catalog

**Date:** July 11, 2026

## A length prefix to glue two independent formats together

`SchemaSection` and `Footer` each know how to serialize themselves, but neither knows about the other, and they get combined into one blob without saying how. I resolved that here, in the catalog, with a plain length prefix in front of the schema bytes so `SplitSchemaAndFooter` knows exactly where one ends and the other begins.

## Reconstruction rebuilds a temporary id-to-table index

`ReconstructFromDisk` builds a short-lived `table_by_id` map purely to attach row groups to the right `TableObject` while walking the flat footer list. It throws if a row group references a table id that isn't in the schema, since that only happens if the file's metadata is already inconsistent, and it's better to fail clearly there than silently drop rows or attach them to the wrong table.
