import os
import platform
import re
import shutil
import statistics
import subprocess
import time
from dataclasses import dataclass, field

GNU_TIME = "/usr/bin/time"


@dataclass
class RunResult:
    elapsed_seconds: float
    peak_rss_kb: float | None
    stdout: str
    returncode: int


@dataclass
class RepeatedResult:
    median_seconds: float
    stdev_seconds: float
    samples: list[float]
    peak_rss_kb_median: float | None
    stdout_last: str


_warned_no_gnu_time = False
_warned_no_taskset = False


def _memory_wrap_command(cmd: list[str]) -> tuple[list[str], str]:
    global _warned_no_gnu_time
    if os.path.exists(GNU_TIME):
        if platform.system() == "Darwin":
            return [GNU_TIME, "-l"] + cmd, "bsd"
        return [GNU_TIME, "-v"] + cmd, "gnu"
    if not _warned_no_gnu_time:
        print(
            f"[warn] {GNU_TIME} not found -- memory measurements will be skipped for this run "
            f"(Linux: `apt install time`; macOS: usually preinstalled at this path already)."
        )
        _warned_no_gnu_time = True
    return cmd, "none"


def _parse_peak_rss(stderr: str, mode: str) -> float | None:
    if mode == "gnu":
        m = re.search(r"Maximum resident set size \(kbytes\): (\d+)", stderr)
        return float(m.group(1)) if m else None
    if mode == "bsd":
        m = re.search(r"(\d+)\s+maximum resident set size", stderr)
        return float(int(m.group(1)) / 1024) if m else None
    return None


def _pin_command(cmd: list[str], pin_cpu: bool) -> list[str]:
    global _warned_no_taskset
    if not pin_cpu:
        return cmd
    if platform.system() != "Linux" or shutil.which("taskset") is None:
        if not _warned_no_taskset:
            print(
                "[warn] --pin-cpu requested but `taskset` isn't available on this platform "
                "(Linux-only) -- running unpinned."
            )
            _warned_no_taskset = True
        return cmd
    return ["taskset", "-c", "0"] + cmd


def run_once(
    cmd: list[str],
    stdin_text: str,
    pin_cpu: bool = False,
    measure_memory: bool = False,
    timeout: float | None = None,
) -> RunResult:
    full_cmd = _pin_command(cmd, pin_cpu)
    mode = "none"
    if measure_memory:
        full_cmd, mode = _memory_wrap_command(full_cmd)

    start = time.perf_counter()
    proc = subprocess.run(
        full_cmd,
        input=stdin_text,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )
    elapsed = time.perf_counter() - start

    peak_rss = _parse_peak_rss(proc.stderr, mode) if measure_memory else None
    return RunResult(elapsed, peak_rss, proc.stdout, proc.returncode)


def run_repeated(
    cmd: list[str],
    stdin_text: str,
    repetitions: int = 11,
    discard_first: int = 1,
    pin_cpu: bool = False,
    measure_memory: bool = False,
    timeout: float | None = None,
) -> RepeatedResult:
    samples: list[float] = []
    rss_samples: list[float] = []
    last_stdout = ""
    for i in range(repetitions):
        result = run_once(
            cmd,
            stdin_text,
            pin_cpu=pin_cpu,
            measure_memory=measure_memory,
            timeout=timeout,
        )
        last_stdout = result.stdout
        if i >= discard_first:
            samples.append(result.elapsed_seconds)
            if result.peak_rss_kb is not None:
                rss_samples.append(result.peak_rss_kb)

    return RepeatedResult(
        median_seconds=statistics.median(samples),
        stdev_seconds=statistics.pstdev(samples) if len(samples) > 1 else 0.0,
        samples=samples,
        peak_rss_kb_median=statistics.median(rss_samples) if rss_samples else None,
        stdout_last=last_stdout,
    )


def try_drop_caches() -> bool:
    if platform.system() != "Linux":
        print(
            "[info] --cold-cache requested but only implemented for Linux; skipping, "
            "results below are warm-cache."
        )
        return False
    try:
        subprocess.run(["sync"], check=True)
        with open("/proc/sys/vm/drop_caches", "w", encoding="utf-8") as f:
            f.write("3\n")
        return True
    except PermissionError:
        print(
            "[info] --cold-cache requires root to write /proc/sys/vm/drop_caches "
            "(try: sudo -E python3 run_benchmarks.py --cold-cache ...); skipping, "
            "results below are warm-cache."
        )
        return False
