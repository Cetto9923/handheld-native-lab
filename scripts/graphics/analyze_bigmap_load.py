#!/usr/bin/env python3
"""The cost of one map transition, split over the four places the time can go.

The engine perf window ("NVN PERF v=1", one per 60 frames) is the yardstick; the frame CSV
(CLOCK_MONOTONIC, the same clock as the engine log) says what each of those 60 frames cost.
Window S covers the frames 60*(S-1)+1 .. 60*S, the mapping the plan calibrated against the
CSV frame gaps.

Buckets of a transition window:
  shader   : the "NVN shader slow-load" lines whose frame falls in the window (total_ms)
  pipeline : the window pipeline line: created count and create_ms
  submit   : the CPU time inside vkQueueSubmit over the window
  other    : the wall time above the steady 60 frames, minus the three above

Usage:
  python3 analyze_bigmap_load.py --log <guest_boot.log> --csv <codex-frame-<pid>.csv> [--json out.json]
"""
import argparse
import csv
import json
import os
import re
import sys

WINDOW_FRAMES = 60
STEADY_FRAME_MS = 1000.0 / 30.0

PAIR = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(-?[0-9.]+)")
SLOW_LOAD = re.compile(r"shader slow-load identity=(\S+) stage=(\d+) frame=(\d+) "
                       r"monotonic_begin_ns=(\d+) monotonic_end_ns=(\d+) total_ms=([0-9.]+)")
PERF_V1 = re.compile(r"NVN PERF v=1 (.*)")
PIPELINE = re.compile(r"NVN PERF pipelines_new=(.*)")

KINDS = (("program", "nvnProgramInitialize"), ("memory_pool", "nvnMemoryPoolInitialize"),
         ("texture_pool", "nvnTexturePoolInitialize"), ("texture", "nvnTextureInitialize"),
         ("window", "nvnWindowInitialize"), ("memory_pool_finalize", "nvnMemoryPoolFinalize"))


def pairs(text):
    out = {}
    for key, value in PAIR.findall(text):
        try:
            out[key] = float(value)
        except ValueError:
            out[key] = value
    return out


def parse_log(path):
    """The perf windows, the pipeline line of each window, the shader slow loads and the
    scene-resource events, in the order the log wrote them."""
    windows, pipelines, slow_loads, events = [], [], [], []
    window_index = 0
    with open(path, errors="replace") as source:
        for line in source:
            if "NVN PERF v=1 " in line:
                window_index += 1
                counters = pairs(PERF_V1.search(line).group(1))
                counters["window"] = window_index
                windows.append(counters)
            elif "NVN PERF pipelines_new=" in line:
                counters = pairs(PIPELINE.search(line).group(1))
                counters["window"] = len(pipelines) + 1
                pipelines.append(counters)
            else:
                match = SLOW_LOAD.search(line)
                if match:
                    slow_loads.append({"identity": match.group(1), "stage": int(match.group(2)),
                                       "frame": int(match.group(3)), "begin_ns": int(match.group(4)),
                                       "end_ns": int(match.group(5)), "total_ms": float(match.group(6))})
                    continue
                for name, tag in KINDS:
                    if tag in line:
                        events.append({"kind": name, "window": window_index})
                        break
    return windows, pipelines, slow_loads, events


def parse_csv(path):
    rows = []
    with open(path) as source:
        for row in csv.DictReader(source):
            try:
                rows.append({"frame": int(row["frame"]), "ns": int(row["monotonic_ns"]),
                             "texture_serial": int(row.get("texture_serial") or 0),
                             "draws": int(row.get("draws") or 0),
                             "dispatches": int(row.get("dispatches") or 0),
                             "submissions": int(row.get("submissions") or 0)})
            except (KeyError, ValueError, TypeError):
                continue
    return rows

def analyze(log_path, csv_path):
    windows, pipelines, slow_loads, events = parse_log(log_path)
    rows = parse_csv(csv_path)
    by_frame = {row["frame"]: row for row in rows}
    gaps = {}
    for index in range(1, len(rows)):
        gaps[rows[index]["frame"]] = (rows[index]["ns"] - rows[index - 1]["ns"]) / 1e6
    per_window = []
    for window in windows:
        index = window["window"]
        first = (index - 1) * WINDOW_FRAMES + 1
        last = index * WINDOW_FRAMES
        frame_rows = [by_frame[f] for f in range(first, last + 1) if f in by_frame]
        window_gaps = [gaps[f] for f in range(first, last + 1) if f in gaps]
        slow = [s for s in slow_loads if first <= s["frame"] <= last]
        pipeline = pipelines[index - 1] if index - 1 < len(pipelines) else {}
        kinds = {}
        for event in events:
            if event["window"] == index:
                kinds[event["kind"]] = kinds.get(event["kind"], 0) + 1
        steady = WINDOW_FRAMES * STEADY_FRAME_MS
        wall = float(window.get("wall_ms") or 0.0)
        submit_rate = float(window.get("submit_cpu_ms") or 0.0)
        entry = {
            "window": index,
            "frames": [first, last],
            "wall_ms": wall,
            "fps": float(window.get("fps") or 0.0),
            "csv_frames": len(frame_rows),
            "csv_gap_sum_ms": round(sum(window_gaps), 1),
            "csv_gap_max_ms": round(max(window_gaps), 1) if window_gaps else 0.0,
            "csv_gaps_over_100ms": sum(1 for g in window_gaps if g > 100.0),
            "csv_gap_over_100ms_sum_ms": round(sum(g for g in window_gaps if g > 100.0), 1),
            "csv_draws_mean": round(sum(r["draws"] for r in frame_rows) / len(frame_rows), 1) if frame_rows else 0.0,
            "csv_dispatches_mean": round(sum(r["dispatches"] for r in frame_rows) / len(frame_rows), 1) if frame_rows else 0.0,
            "csv_texture_serial_delta": (frame_rows[-1]["texture_serial"] - frame_rows[0]["texture_serial"]) if frame_rows else 0,
            "submit_cpu_ms_per_frame": submit_rate,
            "present_cpu_ms_per_frame": float(window.get("present_cpu_ms") or 0.0),
            "gpu_ms_per_frame": float(window.get("gpu_ms") or 0.0),
            "sync_rb": float(window.get("sync_rb") or 0.0),
            "sync_wb": float(window.get("sync_wb") or 0.0),
            "ops_per_frame": float(window.get("ops") or 0.0),
            "views_per_frame": float(window.get("views") or 0.0),
            "pipeline_total": float(window.get("pipelines") or 0.0),
            "images_total": float(window.get("images") or 0.0),
            "arena_peak_mb": float(window.get("arena_peak_mb") or 0.0),
            "shader_slow_count": len(slow),
            "shader_slow_ms": round(sum(s["total_ms"] for s in slow), 2),
            "shader_slow_frames": sorted(set(s["frame"] for s in slow)),
            "pipelines_new": float(pipeline.get("pipelines_new") or 0.0),
            "pipeline_created": float(pipeline.get("created") or 0.0),
            "pipeline_create_ms": float(pipeline.get("create_ms") or 0.0),
            "pipeline_create_max_ms": float(pipeline.get("create_max_ms") or 0.0),
            "warm_hits": float(pipeline.get("warm_hits") or 0.0),
            "warm_built": float(pipeline.get("warm_built") or 0.0),
            "warm_ms": float(pipeline.get("warm_ms") or 0.0),
            "async_built": float(pipeline.get("async_built") or 0.0),
            "programs": kinds.get("program", 0),
            "memory_pools": kinds.get("memory_pool", 0),
            "texture_pools": kinds.get("texture_pool", 0),
            "textures": kinds.get("texture", 0),
            "excess_ms": round(wall - steady, 1),
        }
        per_window.append(entry)
    return {
        "log": os.path.abspath(log_path),
        "csv": os.path.abspath(csv_path),
        "windows": len(windows),
        "csv_frames": len(rows),
        "csv_span_s": round((rows[-1]["ns"] - rows[0]["ns"]) / 1e9, 2) if rows else 0.0,
        "slow_loads": len(slow_loads),
        "slow_load_total_ms": round(sum(s["total_ms"] for s in slow_loads), 2),
        "per_window": per_window,
    }


def main():
    parser = argparse.ArgumentParser(description="the map transition cost, four ways")
    parser.add_argument("--log", required=True)
    parser.add_argument("--csv", action="append", required=True)
    parser.add_argument("--json")
    args = parser.parse_args()
    result = {"sessions": [analyze(args.log, csv_path) for csv_path in args.csv]}
    if args.json:
        with open(args.json, "w") as out:
            json.dump(result, out, indent=1)
        print("wrote", args.json)
    for session in result["sessions"]:
        print(session["csv"], session["windows"], "windows", session["csv_frames"], "csv frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())