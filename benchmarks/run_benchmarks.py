import argparse
import json
import os
import shutil
import tempfile
import time

from engines import run_repeated, try_drop_caches
from generate_load import (
    EVENTS_COLUMNS_GISTDB,
    EVENTS_COLUMNS_SQLITE,
    NUM_USERS,
    USERS_COLUMNS_GISTDB,
    USERS_COLUMNS_SQLITE,
    events_insert_statements,
    schema_statements,
    users_insert_statements,
    wide_table_schema_and_insert,
)


def build_database(binary, db_path, gistdb, row_count, join_side_rows=None):
    if os.path.exists(db_path):
        os.remove(db_path)
    events_cols = EVENTS_COLUMNS_GISTDB if gistdb else EVENTS_COLUMNS_SQLITE
    users_cols = USERS_COLUMNS_GISTDB if gistdb else USERS_COLUMNS_SQLITE

    statements = schema_statements(events_cols, users_cols)
    statements += list(users_insert_statements())
    statements += list(events_insert_statements(row_count))
    script = "\n".join(statements) + "\n"

    cmd = [binary, db_path] if gistdb else [binary, db_path]
    start = time.perf_counter()
    import subprocess

    subprocess.run(cmd, input=script, capture_output=True, text=True, check=False)
    elapsed = time.perf_counter() - start

    size_bytes = os.path.getsize(db_path)
    if not gistdb:
        for ext in ("-wal", "-shm", "-journal"):
            p = db_path + ext
            if os.path.exists(p):
                size_bytes += os.path.getsize(p)
    return elapsed, size_bytes


def run_query(binary, db_path, gistdb, sql, repetitions, pin_cpu, measure_memory):
    cmd = [binary, db_path]
    stdin_text = ".timer ON\n" + sql if not gistdb else sql
    if not stdin_text.endswith("\n"):
        stdin_text += "\n"
    return run_repeated(
        cmd,
        stdin_text,
        repetitions=repetitions,
        discard_first=1,
        pin_cpu=pin_cpu,
        measure_memory=measure_memory,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gistdb-bin", required=True)
    parser.add_argument("--sqlite-bin", default=shutil.which("sqlite3") or "sqlite3")
    parser.add_argument(
        "--scales",
        default="10000,100000,1000000",
        help="Comma-separated row counts for the scaling/join benchmarks.",
    )
    parser.add_argument("--query-battery-scale", type=int, default=1_000_000)
    parser.add_argument("--pruning-scale", type=int, default=1_000_000)
    parser.add_argument("--zonemap-scale", type=int, default=1_000_000)
    parser.add_argument("--selectivity-scale", type=int, default=1_000_000)
    parser.add_argument("--wide-columns", default="5,20,50")
    parser.add_argument("--wide-rows", type=int, default=100_000)
    parser.add_argument("--repetitions", type=int, default=11)
    parser.add_argument("--pin-cpu", action="store_true")
    parser.add_argument("--cold-cache", action="store_true")
    parser.add_argument("--measure-memory", action="store_true")
    parser.add_argument("--out", default="benchmarks/results.json")
    args = parser.parse_args()

    if not shutil.which(args.sqlite_bin) and not os.path.exists(args.sqlite_bin):
        raise SystemExit(
            f"sqlite3 not found at '{args.sqlite_bin}' -- install it or pass "
            f"--sqlite-bin explicitly."
        )

    scales = [int(s) for s in args.scales.split(",")]
    results: dict = {"scales": scales, "categories": {}}
    workdir = tempfile.mkdtemp(prefix="gistdb_bench_")
    print(f"Working directory: {workdir}")

    def maybe_drop_caches():
        if args.cold_cache:
            try_drop_caches()

    load_results = {"gistdb": [], "sqlite": []}
    for n in scales:
        for gistdb, key, binary in (
            (True, "gistdb", args.gistdb_bin),
            (False, "sqlite", args.sqlite_bin),
        ):
            db_path = os.path.join(workdir, f"{key}_{n}.db")
            maybe_drop_caches()
            elapsed, size_bytes = build_database(binary, db_path, gistdb, n)
            load_results[key].append(
                {
                    "rows": n,
                    "seconds": elapsed,
                    "rows_per_sec": n / elapsed if elapsed > 0 else None,
                    "size_bytes": size_bytes,
                }
            )
            print(
                f"[load] {key} n={n}: {elapsed:.3f}s ({n / elapsed:,.0f} rows/s), "
                f"{size_bytes / 1e6:.2f} MB"
            )
    results["categories"]["load_and_storage"] = load_results

    # query scaling (COUNT(*) + SUM at every scale)
    scaling = {"gistdb": [], "sqlite": []}
    query = "SELECT COUNT(*), SUM(amount) FROM events;"
    for n in scales:
        for gistdb, key, binary in (
            (True, "gistdb", args.gistdb_bin),
            (False, "sqlite", args.sqlite_bin),
        ):
            db_path = os.path.join(workdir, f"{key}_{n}.db")
            maybe_drop_caches()
            r = run_query(
                binary,
                db_path,
                gistdb,
                query,
                args.repetitions,
                args.pin_cpu,
                args.measure_memory,
            )
            scaling[key].append(
                {
                    "rows": n,
                    "median_seconds": r.median_seconds,
                    "stdev_seconds": r.stdev_seconds,
                    "peak_rss_kb": r.peak_rss_kb_median,
                }
            )
            print(
                f"[scaling] {key} n={n}: median={r.median_seconds:.4f}s "
                f"stdev={r.stdev_seconds:.4f}s"
            )
    results["categories"]["scaling"] = scaling

    # query battery, at one fixed larger scale
    battery_n = args.query_battery_scale
    battery_queries = {
        "count_star": "SELECT COUNT(*) FROM events;",
        "sum": "SELECT SUM(amount) FROM events;",
        "projection": "SELECT id, category FROM events;",
        "selection": "SELECT id FROM events WHERE amount > 5000;",
        "join": "SELECT users.name, events.amount FROM users JOIN events "
        "ON users.id = events.user_id WHERE events.amount > 9000;",
        "group_by": "SELECT category, SUM(amount) FROM events GROUP BY category;",
        "aggregate_multi": "SELECT category, COUNT(*), SUM(amount), MIN(amount), MAX(amount) "
        "FROM events GROUP BY category;",
    }
    battery = {"rows": battery_n, "gistdb": {}, "sqlite": {}}
    for gistdb, key, binary in (
        (True, "gistdb", args.gistdb_bin),
        (False, "sqlite", args.sqlite_bin),
    ):
        db_path = os.path.join(workdir, f"{key}_{battery_n}.db")
        if not os.path.exists(db_path):
            build_database(binary, db_path, gistdb, battery_n)
        for qname, sql in battery_queries.items():
            maybe_drop_caches()
            r = run_query(
                binary,
                db_path,
                gistdb,
                sql,
                args.repetitions,
                args.pin_cpu,
                args.measure_memory,
            )
            battery[key][qname] = {
                "median_seconds": r.median_seconds,
                "stdev_seconds": r.stdev_seconds,
            }
            print(f"[battery] {key}/{qname}: median={r.median_seconds:.4f}s")
    results["categories"]["query_battery"] = battery

    # column pruning 
    pruning_n = args.pruning_scale
    pruning = {"rows": pruning_n, "gistdb": {}, "sqlite": {}}
    narrow_query = "SELECT COUNT(*) FROM events;" 
    wide_query = (
        "SELECT SUM(amount), SUM(m1), SUM(m2), SUM(m3), SUM(m4), SUM(m5) "
        "FROM events;"
    )
    for gistdb, key, binary in (
        (True, "gistdb", args.gistdb_bin),
        (False, "sqlite", args.sqlite_bin),
    ):
        db_path = os.path.join(workdir, f"{key}_{pruning_n}.db")
        if not os.path.exists(db_path):
            build_database(binary, db_path, gistdb, pruning_n)
        maybe_drop_caches()
        narrow = run_query(
            binary,
            db_path,
            gistdb,
            narrow_query,
            args.repetitions,
            args.pin_cpu,
            args.measure_memory,
        )
        maybe_drop_caches()
        wide = run_query(
            binary,
            db_path,
            gistdb,
            wide_query,
            args.repetitions,
            args.pin_cpu,
            args.measure_memory,
        )
        pruning[key] = {
            "narrow_seconds": narrow.median_seconds,
            "wide_seconds": wide.median_seconds,
        }
        print(
            f"[pruning] {key}: narrow={narrow.median_seconds:.4f}s wide={wide.median_seconds:.4f}s"
        )
    results["categories"]["column_pruning"] = pruning

    # zone-map skip 
    zonemap_n = args.zonemap_scale
    zonemap = {"rows": zonemap_n, "gistdb": {}, "sqlite": {}}
    skip_all_query = f"SELECT COUNT(*) FROM events WHERE id > {zonemap_n + 1000000};"
    skip_none_query = "SELECT COUNT(*) FROM events WHERE id > -1;"
    for gistdb, key, binary in (
        (True, "gistdb", args.gistdb_bin),
        (False, "sqlite", args.sqlite_bin),
    ):
        db_path = os.path.join(workdir, f"{key}_{zonemap_n}.db")
        if not os.path.exists(db_path):
            build_database(binary, db_path, gistdb, zonemap_n)
        maybe_drop_caches()
        skip_all = run_query(
            binary,
            db_path,
            gistdb,
            skip_all_query,
            args.repetitions,
            args.pin_cpu,
            args.measure_memory,
        )
        maybe_drop_caches()
        skip_none = run_query(
            binary,
            db_path,
            gistdb,
            skip_none_query,
            args.repetitions,
            args.pin_cpu,
            args.measure_memory,
        )
        zonemap[key] = {
            "skip_all_seconds": skip_all.median_seconds,
            "skip_none_seconds": skip_none.median_seconds,
        }
        print(
            f"[zonemap] {key}: skip_all={skip_all.median_seconds:.4f}s "
            f"skip_none={skip_none.median_seconds:.4f}s"
        )
    results["categories"]["zone_map"] = zonemap

    # selectivity 
    selectivity_n = args.selectivity_scale
    selectivity = {"rows": selectivity_n, "gistdb": [], "sqlite": []}
    thresholds = [
        (100, "1%"),
        (1000, "10%"),
        (2500, "25%"),
        (5000, "50%"),
        (7500, "75%"),
        (9999, "100%"),
    ]
    for gistdb, key, binary in (
        (True, "gistdb", args.gistdb_bin),
        (False, "sqlite", args.sqlite_bin),
    ):
        db_path = os.path.join(workdir, f"{key}_{selectivity_n}.db")
        if not os.path.exists(db_path):
            build_database(binary, db_path, gistdb, selectivity_n)
        for threshold, label in thresholds:
            sql = f"SELECT COUNT(*) FROM events WHERE amount < {threshold};"
            maybe_drop_caches()
            r = run_query(
                binary,
                db_path,
                gistdb,
                sql,
                args.repetitions,
                args.pin_cpu,
                args.measure_memory,
            )
            selectivity[key].append(
                {"selectivity": label, "median_seconds": r.median_seconds}
            )
            print(f"[selectivity] {key} {label}: median={r.median_seconds:.4f}s")
    results["categories"]["selectivity"] = selectivity

    #  join scaling
    join_scaling = {"gistdb": [], "sqlite": []}
    join_query = (
        "SELECT users.name, events.amount FROM users JOIN events "
        "ON users.id = events.user_id;"
    )
    for n in scales:
        for gistdb, key, binary in (
            (True, "gistdb", args.gistdb_bin),
            (False, "sqlite", args.sqlite_bin),
        ):
            db_path = os.path.join(workdir, f"{key}_{n}.db")
            maybe_drop_caches()
            r = run_query(
                binary,
                db_path,
                gistdb,
                join_query,
                args.repetitions,
                args.pin_cpu,
                args.measure_memory,
            )
            join_scaling[key].append({"rows": n, "median_seconds": r.median_seconds})
            print(f"[join] {key} n={n}: median={r.median_seconds:.4f}s")
    results["categories"]["join_scaling"] = join_scaling

    # wide-table benchmark 
    wide_columns = [int(c) for c in args.wide_columns.split(",")]
    wide_results = {"rows": args.wide_rows, "gistdb": [], "sqlite": []}
    for num_cols in wide_columns:
        for gistdb, key, binary in (
            (True, "gistdb", args.gistdb_bin),
            (False, "sqlite", args.sqlite_bin),
        ):
            db_path = os.path.join(workdir, f"{key}_wide_{num_cols}.db")
            if os.path.exists(db_path):
                os.remove(db_path)
            table_name, schema, inserts = wide_table_schema_and_insert(
                num_cols, args.wide_rows, gistdb
            )
            script = schema + "\n" + "\n".join(inserts) + "\n"
            import subprocess

            subprocess.run(
                [binary, db_path],
                input=script,
                capture_output=True,
                text=True,
                check=False,
            )
            maybe_drop_caches()
            sql = (
                f"SELECT c3 FROM {table_name};"
                if num_cols > 3
                else f"SELECT c0 FROM {table_name};"
            )
            r = run_query(
                binary,
                db_path,
                gistdb,
                sql,
                args.repetitions,
                args.pin_cpu,
                args.measure_memory,
            )
            wide_results[key].append(
                {"columns": num_cols, "median_seconds": r.median_seconds}
            )
            print(f"[wide] {key} cols={num_cols}: median={r.median_seconds:.4f}s")
    results["categories"]["wide_table"] = wide_results

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
    print(f"\nWrote {args.out}")
    shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
