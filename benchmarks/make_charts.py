import argparse
import json
import os
import matplotlib.pyplot as plt


def scaling_chart(data, out_dir):
    scaling = data["categories"]["scaling"]
    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sorted(scaling[key], key=lambda p: p["rows"])
        ax.plot(
            [p["rows"] for p in points],
            [p["median_seconds"] for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xscale("log")
    ax.set_xlabel("Rows")
    ax.set_ylabel("Query time (seconds, median of repeated runs)")
    ax.set_title("Scaling: COUNT(*) + SUM(amount) vs. row count")
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "01_scaling.png"), dpi=150)
    plt.close(fig)


def query_battery_chart(data, out_dir):
    battery = data["categories"]["query_battery"]
    names = list(battery["gistdb"].keys())
    gistdb_times = [battery["gistdb"][n]["median_seconds"] for n in names]
    sqlite_times = [battery["sqlite"][n]["median_seconds"] for n in names]

    x = range(len(names))
    width = 0.35
    fig, ax = plt.subplots(figsize=(9, 5))
    ax.bar([i - width / 2 for i in x], gistdb_times, width, label="GistDB")
    ax.bar([i + width / 2 for i in x], sqlite_times, width, label="SQLite")
    ax.set_xticks(list(x))
    ax.set_xticklabels(names, rotation=30, ha="right")
    ax.set_ylabel("Query time (seconds)")
    ax.set_title(f"Query battery at {battery['rows']:,} rows")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "02_query_battery.png"), dpi=150)
    plt.close(fig)


def column_pruning_chart(data, out_dir):
    pruning = data["categories"]["column_pruning"]
    labels = ["Narrow\n(COUNT(*), 0 cols)", "Wide\n(6 of 11 cols)"]
    fig, ax = plt.subplots(figsize=(6, 5))
    x = range(len(labels))
    width = 0.35
    ax.bar(
        [i - width / 2 for i in x],
        [pruning["gistdb"]["narrow_seconds"], pruning["gistdb"]["wide_seconds"]],
        width,
        label="GistDB",
    )
    ax.bar(
        [i + width / 2 for i in x],
        [pruning["sqlite"]["narrow_seconds"], pruning["sqlite"]["wide_seconds"]],
        width,
        label="SQLite",
    )
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.set_ylabel("Query time (seconds)")
    ax.set_title(f"Column pruning effect at {pruning['rows']:,} rows")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "03_column_pruning.png"), dpi=150)
    plt.close(fig)


def zone_map_chart(data, out_dir):
    zm = data["categories"]["zone_map"]
    labels = ["Skip all row groups\n(WHERE id > max)", "Skip nothing\n(WHERE id > -1)"]
    fig, ax = plt.subplots(figsize=(6, 5))
    x = range(len(labels))
    width = 0.35
    ax.bar(
        [i - width / 2 for i in x],
        [zm["gistdb"]["skip_all_seconds"], zm["gistdb"]["skip_none_seconds"]],
        width,
        label="GistDB",
    )
    ax.bar(
        [i + width / 2 for i in x],
        [zm["sqlite"]["skip_all_seconds"], zm["sqlite"]["skip_none_seconds"]],
        width,
        label="SQLite (no zone maps)",
    )
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.set_ylabel("Query time (seconds)")
    ax.set_title(f"Zone-map row-group skipping at {zm['rows']:,} rows")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "04_zone_map.png"), dpi=150)
    plt.close(fig)


def selectivity_chart(data, out_dir):
    sel = data["categories"]["selectivity"]
    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sel[key]
        ax.plot(
            [p["selectivity"] for p in points],
            [p["median_seconds"] for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xlabel("Predicate selectivity (rows surviving WHERE clause)")
    ax.set_ylabel("Query time (seconds)")
    ax.set_title(
        f"Selectivity vs. query time at {sel['rows']:,} rows\n"
        "(row-level filter cost, not zone-map skip -- see benchmark script comments)"
    )
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "05_selectivity.png"), dpi=150)
    plt.close(fig)


def join_scaling_chart(data, out_dir):
    js = data["categories"]["join_scaling"]
    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sorted(js[key], key=lambda p: p["rows"])
        ax.plot(
            [p["rows"] for p in points],
            [p["median_seconds"] for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xscale("log")
    ax.set_xlabel("Rows on the events side (users fixed at 1,000)")
    ax.set_ylabel("Query time (seconds)")
    ax.set_title("Hash join scaling")
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "06_join_scaling.png"), dpi=150)
    plt.close(fig)


def wide_table_chart(data, out_dir):
    wt = data["categories"]["wide_table"]
    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sorted(wt[key], key=lambda p: p["columns"])
        ax.plot(
            [p["columns"] for p in points],
            [p["median_seconds"] for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xlabel("Table width (number of columns)")
    ax.set_ylabel(f"Query time selecting 1 column, at {wt['rows']:,} rows (seconds)")
    ax.set_title("Wide-table benchmark: does querying 1-of-N columns stay flat?")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "07_wide_table.png"), dpi=150)
    plt.close(fig)


def load_and_storage_charts(data, out_dir):
    load = data["categories"]["load_and_storage"]

    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sorted(load[key], key=lambda p: p["rows"])
        ax.plot(
            [p["rows"] for p in points],
            [p["rows_per_sec"] for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xscale("log")
    ax.set_xlabel("Rows loaded")
    ax.set_ylabel("Rows inserted / second")
    ax.set_title("Insert throughput")
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "08_insert_throughput.png"), dpi=150)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sorted(load[key], key=lambda p: p["rows"])
        ax.plot(
            [p["rows"] for p in points],
            [p["size_bytes"] / 1e6 for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xscale("log")
    ax.set_xlabel("Rows loaded")
    ax.set_ylabel("Database file size (MB)")
    ax.set_title("On-disk size")
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "09_storage_size.png"), dpi=150)
    plt.close(fig)


def memory_chart(data, out_dir):
    scaling = data["categories"]["scaling"]
    if not any(p.get("peak_rss_kb") for p in scaling["gistdb"] + scaling["sqlite"]):
        return
    fig, ax = plt.subplots(figsize=(7, 5))
    for key, label, marker in (("gistdb", "GistDB", "o"), ("sqlite", "SQLite", "s")):
        points = sorted(scaling[key], key=lambda p: p["rows"])
        ax.plot(
            [p["rows"] for p in points],
            [p["peak_rss_kb"] / 1024 for p in points],
            marker=marker,
            label=label,
        )
    ax.set_xscale("log")
    ax.set_xlabel("Rows")
    ax.set_ylabel("Peak resident memory (MB)")
    ax.set_title("Peak memory usage during query execution")
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "10_memory_usage.png"), dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", default="benchmarks/results.json")
    parser.add_argument("--out-dir", default="benchmarks/charts")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    with open(args.results, encoding="utf-8") as f:
        data = json.load(f)

    scaling_chart(data, args.out_dir)
    query_battery_chart(data, args.out_dir)
    column_pruning_chart(data, args.out_dir)
    zone_map_chart(data, args.out_dir)
    selectivity_chart(data, args.out_dir)
    join_scaling_chart(data, args.out_dir)
    wide_table_chart(data, args.out_dir)
    load_and_storage_charts(data, args.out_dir)
    memory_chart(data, args.out_dir)
    print(f"Charts written to {args.out_dir}/")


if __name__ == "__main__":
    main()
