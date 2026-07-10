<div align="center">
# GistDB

[![CI](https://github.com/priyansh-dimri/gistdb/actions/workflows/ci.yml/badge.svg)](https://github.com/priyansh-dimri/gistdb/actions/workflows/ci.yml)
[![Lint](https://github.com/priyansh-dimri/gistdb/actions/workflows/lint.yml/badge.svg)](https://github.com/priyansh-dimri/gistdb/actions/workflows/lint.yml)
[![codecov](https://codecov.io/gh/priyansh-dimri/gistdb/graph/badge.svg)](https://codecov.io/gh/priyansh-dimri/gistdb)

</div>

GistDB is a single-threaded analytical database engine that stores data column-by-column instead of row-by-row, and processes it in batches of rows at a time rather than one row at a time.

It is implemented from scratch in modern C++ with an on-disk storage format, buffer pool manager, rule-based query optimizer, and vectorized execution engine.

## What it does

- Stores tables in a custom columnar file format
- Parses real SQL via the `libpg_query` library.
- Supports `CREATE TABLE`, `INSERT`, and `SELECT` with filtering, joins, grouping, and aggregates
- Optimizes queries with predicate push down and column pruning before execution
- Executes queries using a vectorized, pull based engine that processes 1,024 rows at a time

## What it does NOT do

- No `UPDATE` or `DELETE` queries.
- No transactions and no concurrency.
- No crash recovery, thus no write-ahead log.
- No network layer. It only runs as a local CLI, SQL typed in via standard input.

## Tech Stack

- **Language:** C++23
- **Build system:** CMake
- **SQL parsing:** libpg_query
- **Testing:** Google Test
- **Linting:** Clang-Tidy, Clang-Format
