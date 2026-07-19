# GistDB Benchmark Report

All benchmarks use the median of N repeated runs. The first run is discarded as a warm-up. Timings are measured externally using Python's `time.perf_counter()` around each process execution, rather than relying on query times reported by either engine. The benchmark scripts are available in `benchmarks/`.

## Scaling

![scaling](charts/01_scaling.png)

## Query Workloads

![query battery](charts/02_query_battery.png)

## Column Pruning

![column pruning](charts/03_column_pruning.png)

## Zone-Map Row-Group Skipping

![zone map](charts/04_zone_map.png)

## Predicate Selectivity

Measures the cost of filtering rows at different selectivity levels. This benchmark focuses on row-level filtering and does not measure zone-map skipping, since the test data is not clustered by the filtered column.

![selectivity](charts/05_selectivity.png)

## Join Scaling

![join scaling](charts/06_join_scaling.png)

## Wide-Table Benchmark

![wide table](charts/07_wide_table.png)

## Insert Throughput & Storage Size

![insert throughput](charts/08_insert_throughput.png)
![storage size](charts/09_storage_size.png)

## Memory Usage

![memory usage](charts/10_memory_usage.png)
