<div align="center">

# GistDB

[![CI](https://github.com/priyansh-dimri/gistdb/actions/workflows/ci.yml/badge.svg)](https://github.com/priyansh-dimri/gistdb/actions/workflows/ci.yml)
[![Lint](https://github.com/priyansh-dimri/gistdb/actions/workflows/lint.yml/badge.svg)](https://github.com/priyansh-dimri/gistdb/actions/workflows/lint.yml)
[![codecov](https://codecov.io/gh/priyansh-dimri/gistdb/graph/badge.svg)](https://codecov.io/gh/priyansh-dimri/gistdb)

</div>

GistDB is a single-threaded analytical database engine that stores data **column-by-column** instead of row-by-row and processes query results in vectorized batches of up to 1,024 rows at a time. It follows the architectural paradigms of modern OLAP systems like DuckDB and ClickHouse, optimizing analytical workloads through columnar storage and batch-oriented execution rather than traditional row-at-a-time processing.

With the sole exception of `libpg_query` (used strictly to translate raw SQL strings into an Abstract Syntax Tree), the entire data stack is implemented from the ground up. This includes a custom columnar storage format, a buffer pool manager, a rule-based query optimizer and a pull-based vectorized execution pipeline.

## Architectural Design Decisions

The structural decisions are documented in [`docs/decisions/`](docs/decisions/). Some of the major ones are briefly summarized below::

- **Hardware-Aligned Columnar Layout:** A 4KB storage page holds exactly 1,024 `int32`/`float32` values. This perfectly matches the execution engine's 1,024-row batch size by construction, meaning one disk page strictly translates to one execution vector.
- **Zero-Copy by Default:** Most execution operators read directly from memory pinned by the Buffer Pool. There are only two disclosed exceptions: Hash Join's build side (which must outlive the buffer pool's eviction policy across the entire probe phase) and Projection (the point where data permanently leaves the engine).
- **Deterministic Rule-Based Optimization:** The engine utilizes a two-pass rule-based optimizer. Predicate pushdown and column pruning run exactly once, in a fixed order, ensuring stable execution plans without the overhead of cardinality estimation or statistical plan searches.
- **Zone-Map I/O Pruning:** A simple `column > constant` predicate can safely eliminate an entire 10,240-row chunk from the execution pipeline by evaluating footer metadata already resident in memory, preventing unnecessary disk I/O.
- **Append-Only Storage:** Because `UPDATE` and `DELETE` operations are intentionally out of scope, there is no need for LSM-Tree compaction or tombstone tracking. The storage engine writes to a single growing file, architecturally closer to Apache Parquet.

## Quick Start

```bash
git clone https://github.com/priyansh-dimri/gistdb.git
cd gistdb
git submodule update --init --recursive

# Build libpg_query dependency
cd third_party/libpg_query
make
cd ../..

# Build the GistDB engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Launch the REPL
./build/gistdb mydb.gistdb
```

The CLI drops you into a fully functional REPL:

```sql
GistDB> CREATE TABLE users (id int4, name varchar);
Table created (id=0).

GistDB> INSERT INTO users (id, name)
      > VALUES (1, 'alice'), (2, 'bob'), (3, 'carol');
3 row(s) inserted (0.000s)

GistDB> SELECT id, name FROM users WHERE id > 1;
+----+-------+
| id | name  |
+----+-------+
| 2  | bob   |
| 3  | carol |
+----+-------+
(2 rows returned in 0.000s)
```

## Usage Examples

### Filtering and Projection

```sql
SELECT name FROM users WHERE id > 1;
```

### In-Memory Hash Joins

```sql
CREATE TABLE orders (id int4, user_id int4, amount int4);
INSERT INTO orders (id, user_id, amount) VALUES (100, 1, 50), (101, 2, 30);
SELECT users.name, orders.amount
FROM users JOIN orders
ON users.id = orders.user_id;
```

### Aggregation and Grouping

```sql
SELECT user_id, SUM(amount) FROM orders GROUP BY user_id;
```

### Headless Execution

Piping input or redirecting output automatically switches the engine to plain tab-separated (TSV) rows instead of a formatted boxed table, mimicking the behavior of standard `psql` or `mysql` clients in non-TTY environments:

```bash
echo "SELECT * FROM users;" | ./build/gistdb mydb.gistdb > out.tsv
```

## Running with Docker

```bash
docker build -t gistdb .
docker run -it --rm -v "$(pwd)/data:/data" gistdb /data/mydb.gistdb
```

## Scope Boundaries

### What it does

- Stores tables in a custom columnar file format.
- Parses SQL via `libpg_query`.
- Executes `CREATE TABLE`, `INSERT`, and `SELECT` (Filters, Joins, Grouping, Aggregates).
- Skips entire row groups via zone-map metadata before touching disk.

### What it doesn't do

- **No `UPDATE` or `DELETE`:** Append-only analytical workloads only.
- **No Concurrency:** Single-threaded execution; no ACID transaction overhead.
- **No Crash Recovery:** No Write-Ahead Logging (WAL).
- **No Network Stack:** Local CLI execution only; SQL parsing via standard input.

## Performance & Benchmarks

GistDB was benchmarked against SQLite. The goal of this comparison was to empirically validate that the architectural theories of OLAP (Columnar Layouts and Zone-Maps) behave as expected.

The full methodology, charts, and analysis are available in the [Benchmark Report](benchmarks/report.md).

### Key Architectural Takeaways

- **Zone-Map I/O Pruning Works:** On a 1,000,000-row dataset where all rows are filtered out by a simple predicate (`WHERE id > max`), GistDB correctly evaluates the footer metadata and skips touching the disk entirely. GistDB executed this in **0.006s**, while SQLite (which lacks zone-maps) took **0.066s**—an 11x speedup for this workload.
- **Column Pruning Impact:** The benchmarks demonstrate the performance difference between querying narrow (1 column) and wide (6 columns) data in a columnar format, showing the advantage of avoiding unnecessary column reads.
- **Aggregation and Join Performance:** GistDB currently trails behind SQLite in aggregation and join benchmarks.

## Tech Stack

- **Language:** C++23
- **Build System:** CMake
- **AST Parser:** libpg_query
- **Testing:** Google Test (GTest)
- **Code Quality:** Clang-Tidy, Clang-Format
