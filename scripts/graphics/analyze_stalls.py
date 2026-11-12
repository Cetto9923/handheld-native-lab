#!/usr/bin/env python3
"""The acceptance metrics of the frame timing, over a whole run or over a window of one.

The numbers are the ones every acceptance of this port uses: the steady frame gap, the frames that stall for more than
100 ms, and the share of the session those stalls take. The definitions are the ones of the analyzer the acceptance has
been using, so a run before and a run after are directly comparable.

Usage:
  python3 analyze_stalls.py <codex-frame-<pid>.csv> [--baseline] [--start-ns N] [--end-ns N] [--json out.json]
  python3 analyze_stalls.py --perf-log <guest_boot.log> [--perf-threshold-ms 2100] [--json out.json]

The window is given in the clock of the CSV (CLOCK_MONOTONIC, nanoseconds), so the transition segment can be measured
on its own without touching the rest of the session. A screen recording is never used: it samples below the frame rate
and it changes the frame rate it measures.

The second mode reads the engine's own perf windows (the "NVN PERF v=1" lines, one per 60 frames) and compares the
slow windows with the fast ones, over the counters that the map transition was attributed to. The baseline of that
comparison is fixed in this file, so a run before the change and a run after it are read with the same yardstick.
"""
import argparse
import csv
import json
import os
import re
import statistics
import sys

BASELINE = {"stalls_over_100ms": 93, "stall_time_s": 40.1, "stall_share_pct": 13.6, "median_gap_ms": 33.34}

# The perf windows of the run before the readback retirement changed, measured on the Honor tablet's own
# guest_boot.log (123 windows of 60 frames). The slow windows are the ones longer than 2100 ms; the numbers are the
# averages over the slow windows (28 of them) and over the fast ones (95 of them).
PERF_BASELINE = {
    "source": "the Honor tablet A42BBB5109000333, guest_boot.log, 123 perf windows of 60 frames, before this change",
    "slow_windows": {"submit_cpu_ms": 24.35, "sync_rb": 5.04, "gpu_ms": 15.43, "created": 10.68},
    "fast_windows": {"submit_cpu_ms": 13.22, "sync_rb": 0.02, "gpu_ms": 16.82, "created": 0.38},
    "transition_window": {"wall_ms": 3433.7, "fps": 17.47, "submit_cpu_ms": 28.51, "sync_rb": 7},
    "counter_meaning": {
        "submit_cpu_ms": "the time the CPU spends inside vkQueueSubmit, per frame",
        "sync_rb": "the readback drains in the window (the engine's own counter, per window)",
        "gpu_ms": "the GPU's own timeline, per frame",
        "created": "the pipelines the game thread built in the window, per frame",
    },
}

PAIR = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(-?[0-9.]+)")


def load(path, start_ns=None, end_ns=None):
    rows = []
    if not os.path.exists(path):
        raise FileNotFoundError(path)
    with open(path) as source:
        for row in csv.DictReader(source):
            try:
                frame = int(row["frame"])
                stamp = int(row["monotonic_ns"])
            except (KeyError, ValueError, TypeError):
                continue
            if start_ns is not None and stamp < start_ns:
                continue
            if end_ns is not None and stamp > end_ns:
                continue
            rows.append((frame, stamp))
    return rows


def analyze(path, start_ns=None, end_ns=None):
    rows = load(path, start_ns, end_ns)
    if len(rows) < 2:
        print("not enough frames in", path)
        return None
    frames = [r[0] for r in rows]
    stamps = [r[1] for r in rows]
    gaps = [(stamps[i + 1] - stamps[i]) / 1e6 for i in range(len(stamps) - 1)]
    span = (stamps[-1] - stamps[0]) / 1e9
    stalls = [(frames[i + 1], gap) for i, gap in enumerate(gaps) if gap > 100]
    stall_time = sum(gap for _, gap in stalls) / 1000.0
    steady = [gap for gap in gaps if gap <= 50]
    result = {
        "file": path,
        "window": {"start_ns": start_ns, "end_ns": end_ns},
        "frames": len(rows),
        "span_s": round(span, 2),
        "avg_fps": round((len(rows) - 1) / span, 2),
        "median_gap_ms": round(statistics.median(steady), 2) if steady else None,
        "steady_fps": round(1000 / statistics.median(steady), 1) if steady else None,
        "steady_share_pct": round(len(steady) / len(gaps) * 100, 1),
        "stalls_over_100ms": len(stalls),
        "stall_time_s": round(stall_time, 1),
        "stall_share_pct": round(stall_time / span * 100, 1) if span else None,
        "fps_excluding_stalls": round(len(rows) / (span - stall_time), 2) if span > stall_time else None,
        "worst_stalls": [[f, round(g)] for f, g in sorted(stalls, key=lambda t: -t[1])[:10]],
    }
    return result


def parse_perf_log(path):
    """The engine's own perf windows: one "NVN PERF v=1" line per 60 frames.

    The lines that belong to the same window follow each other: the "pipelines_new" line (the pipelines the game
    thread built) and the "readback" line (the readback retirement counters) are attached to the window above them.
    """
    windows = []
    with open(path, errors="replace") as source:
        for line in source:
            if "NVN PERF v=1 " in line:
                window = {k: float(v) for k, v in PAIR.findall(line)}
                window["_raw"] = line.strip()
                windows.append(window)
            elif "NVN PERF pipelines_new=" in line and windows:
                windows[-1].update({k: float(v) for k, v in PAIR.findall(line)})
            elif "NVN PERF readback " in line and windows:
                windows[-1].update({k: float(v) for k, v in PAIR.findall(line)})
    return windows


def average(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return round(sum(values) / len(values), 2)


def compare_windows(windows, threshold_ms=2100.0, counters=("submit_cpu_ms", "sync_rb", "gpu_ms", "created")):
    """The slow windows against the fast ones, over the counters of the attribution."""
    slow = [w for w in windows if w.get("wall_ms", 0.0) > threshold_ms]
    fast = [w for w in windows if w.get("wall_ms", 0.0) <= threshold_ms]
    result = {
        "file": None,
        "threshold_ms": threshold_ms,
        "windows": len(windows),
        "slow_windows": len(slow),
        "fast_windows": len(fast),
        "counters": {},
        "slowest_windows": [
            {
                "wall_ms": round(w.get("wall_ms", 0.0), 1),
                "fps": round(w.get("fps", 0.0), 2),
                **{c: round(w.get(c, 0.0), 2) for c in counters if c in w},
            }
            for w in sorted(windows, key=lambda w: -w.get("wall_ms", 0.0))[:5]
        ],
    }
    for counter in counters:
        result["counters"][counter] = {
            "slow": average([w.get(counter) for w in slow]),
            "fast": average([w.get(counter) for w in fast]),
            "baseline_slow": PERF_BASELINE["slow_windows"].get(counter),
            "baseline_fast": PERF_BASELINE["fast_windows"].get(counter),
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?")
    parser.add_argument("--start-ns", type=int)
    parser.add_argument("--end-ns", type=int)
    parser.add_argument("--json", type=str)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--perf-log", type=str, help="the engine's own guest_boot.log with its perf windows")
    parser.add_argument("--perf-threshold-ms", type=float, default=2100.0)
    args = parser.parse_args()

    if args.perf_log:
        try:
            windows = parse_perf_log(args.perf_log)
        except FileNotFoundError as exc:
            print('no such perf log:', exc)
            return 2
        if not windows:
            print("no perf windows in", args.perf_log)
            return 1
        result = compare_windows(windows, args.perf_threshold_ms)
        result["file"] = args.perf_log
        result["baseline"] = PERF_BASELINE
        for key, value in result.items():
            if key == "counters":
                continue
            print(f"{key}: {value}")
        print("counters (slow / fast, with the baseline before the change):")
        for counter, values in result["counters"].items():
            print(
                f"  {counter}: slow={values['slow']} fast={values['fast']} "
                f"(baseline slow={values['baseline_slow']} fast={values['baseline_fast']})"
            )
        print("slowest windows:")
        for window in result["slowest_windows"]:
            print("  ", window)
        if args.json:
            with open(args.json, "w") as out:
                json.dump(result, out, ensure_ascii=False, indent=2)
        return 0

    if not args.input:
        parser.error("an input CSV or --perf-log is required")
    try:
        result = analyze(args.input, args.start_ns, args.end_ns)
    except FileNotFoundError as exc:
        print('no such frame CSV:', exc)
        return 2
    if not result:
        return 1
    for key, value in result.items():
        print(f"{key}: {value}")
    if args.baseline:
        print("\nbaseline (pre-fix, 295.2 s session):", BASELINE)
    if args.json:
        with open(args.json, "w") as out:
            json.dump(result, out, ensure_ascii=False, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
