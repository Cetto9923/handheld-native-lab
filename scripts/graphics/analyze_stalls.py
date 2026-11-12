#!/usr/bin/env python3
"""The acceptance metrics of the frame timing, over a whole run or over a window of one.

The numbers are the ones every acceptance of this port uses: the steady frame gap, the frames that stall for more than
100 ms, and the share of the session those stalls take. The definitions are the ones of the analyzer the acceptance has
been using, so a run before and a run after are directly comparable.

Usage:
  python3 analyze_stalls.py <codex-frame-<pid>.csv> [--baseline] [--start-ns N] [--end-ns N] [--json out.json]

The window is given in the clock of the CSV (CLOCK_MONOTONIC, nanoseconds), so the transition segment can be measured
on its own without touching the rest of the session. A screen recording is never used: it samples below the frame rate
and it changes the frame rate it measures.
"""
import argparse
import csv
import json
import os
import statistics
import sys

BASELINE = {"stalls_over_100ms": 93, "stall_time_s": 40.1, "stall_share_pct": 13.6, "median_gap_ms": 33.34}


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("--start-ns", type=int)
    parser.add_argument("--end-ns", type=int)
    parser.add_argument("--json", type=str)
    parser.add_argument("--baseline", action="store_true")
    args = parser.parse_args()
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
