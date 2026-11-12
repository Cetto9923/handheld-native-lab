#!/usr/bin/env python3
"""The cost of one scene transition, split over the four places the time can go.

The engine perf window ("NVN PERF v=1", one per 60 frames) is the yardstick; the frame CSV (CLOCK_MONOTONIC, the
same clock as the engine log) says what each of those 60 frames cost. Window S covers the frames 60*(S-1)+1 .. 60*S,
the mapping the plan calibrated against the CSV frame gaps.

Buckets of a transition (a cluster of windows whose wall time is above the steady 60 frames):
  shader   : the shader loads in the cluster (the slow ones carry their own frame, the rest are bracketed by them)
  pipeline : the pipeline creations of the cluster (created count and create_ms)
  submit   : the CPU time inside vkQueueSubmit over the cluster, and the readback/writeback drains
  other    : the wall time above the steady state, minus the shader and the pipeline time

The four buckets overlap in time (the CPU inside vkQueueSubmit overlaps the GPU wait and the
guest's own work), so they must not be added up: "other" can come out negative when the
blocking wait is longer than the excess. The number to read is the graphics path's own share,
(shader + pipeline) / excess.

Usage:
  python3 analyze_bigmap_load.py --log <guest_boot.log> --csv <codex-frame-<pid>.csv> [--json out.json] [--report]
"""
import argparse
import csv
import json
import os
import re
import sys

WINDOW_FRAMES = 60
STEADY_FRAME_MS = 1000.0 / 30.0
STALL_MS = 100.0

PAIR = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(-?[0-9.]+)")
SLOW_LOAD = re.compile(r"shader slow-load identity=(\S+) stage=(\d+) frame=(\d+) "
                       r"monotonic_begin_ns=(\d+) monotonic_end_ns=(\d+) total_ms=([0-9.]+)")
PERF_V1 = re.compile(r"NVN PERF v=1 (.*)")
PIPELINE = re.compile(r"NVN PERF pipelines_new=(.*)")
SHADER_LOADED = re.compile(r"NVN shader (\w+) stage=(\d+) loaded words=(\d+) ms read=([0-9.]+) "
                           r"rewrite=([0-9.]+) analyse=([0-9.]+) module=([0-9.]+) reused=(\d+)")
# the blocking sites of the perf window: "site=ms/calls" per frame, then legacy_wait_total, then the submit shares
BLOCK_SITE = re.compile(r"(other|ring|present|event|arena|gpu_sync|complete|poll|legacy_wait)=([0-9.]+)/([0-9.]+)")

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
    """The perf windows, the pipeline line of each window, the shader slow loads, the shader load lines and the
    scene-resource events, in the order the log wrote them."""
    windows, pipelines, slow_loads, events, loaded = [], [], [], [], []
    blocks = []
    window_index = 0
    anchor_frame = None
    with open(path, errors="replace") as source:
        for line in source:
            if "NVN PERF v=1 " in line:
                window_index += 1
                counters = pairs(PERF_V1.search(line).group(1))
                counters["window"] = window_index
                windows.append(counters)
            elif "NVN PERF block_ms " in line:
                sites = {}
                head = line.split(" submit_by")[0]
                for name, ms, calls in BLOCK_SITE.findall(head):
                    sites[name] = float(ms)
                blocks.append(sites)
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
                    anchor_frame = int(match.group(3))
                    continue
                match = SHADER_LOADED.search(line)
                if match:
                    # the frame of this load is the last slow-load anchor before it: the two lines are adjacent for a
                    # slow load, and a fast load sits in the same frame segment as the anchor around it
                    read_ms = float(match.group(4)) + float(match.group(5)) + float(match.group(6)) + float(match.group(7))
                    loaded.append({"id": match.group(1), "stage": int(match.group(2)), "ms": read_ms,
                                   "read_ms": float(match.group(4)), "rewrite_ms": float(match.group(5)),
                                   "analyse_ms": float(match.group(6)), "module_ms": float(match.group(7)),
                                   "reused": int(match.group(8)), "anchor_frame": anchor_frame})
                    continue
                for name, tag in KINDS:
                    if tag in line:
                        events.append({"kind": name, "window": window_index})
                        break
    return windows, pipelines, slow_loads, loaded, events, blocks


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
    windows, pipelines, slow_loads, loaded, events, blocks = parse_log(log_path)
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
        shaders = [s for s in loaded if s["anchor_frame"] is not None and first <= s["anchor_frame"] <= last]
        pipeline = pipelines[index - 1] if index - 1 < len(pipelines) else {}
        kinds = {}
        for event in events:
            if event["window"] == index:
                kinds[event["kind"]] = kinds.get(event["kind"], 0) + 1
        steady = WINDOW_FRAMES * STEADY_FRAME_MS
        wall = float(window.get("wall_ms") or 0.0)
        entry = {
            "window": index,
            "frames": [first, last],
            "wall_ms": wall,
            "fps": float(window.get("fps") or 0.0),
            "csv_frames": len(frame_rows),
            "csv_gap_sum_ms": round(sum(window_gaps), 1),
            "csv_gap_max_ms": round(max(window_gaps), 1) if window_gaps else 0.0,
            "csv_gaps_over_100ms": sum(1 for g in window_gaps if g > STALL_MS),
            "csv_gap_over_100ms_sum_ms": round(sum(g for g in window_gaps if g > STALL_MS), 1),
            "csv_draws_total": (frame_rows[-1]["draws"] - frame_rows[0]["draws"]) if frame_rows else 0,
            "csv_dispatches_total": (frame_rows[-1]["dispatches"] - frame_rows[0]["dispatches"]) if frame_rows else 0,
            "csv_texture_serial_delta": (frame_rows[-1]["texture_serial"] - frame_rows[0]["texture_serial"]) if frame_rows else 0,
            "submit_cpu_ms_per_frame": float(window.get("submit_cpu_ms") or 0.0),
            "present_cpu_ms_per_frame": float(window.get("present_cpu_ms") or 0.0),
            "gpu_ms_per_frame": float(window.get("gpu_ms") or 0.0),
            "sync_rb": float(window.get("sync_rb") or 0.0),
            "sync_wb": float(window.get("sync_wb") or 0.0),
            "ops_per_frame": float(window.get("ops") or 0.0),
            "block_ms_per_frame": round(sum(blocks[index - 1].values()), 2) if index - 1 < len(blocks) else 0.0,
            "block_sites": blocks[index - 1] if index - 1 < len(blocks) else {},
            "views_per_frame": float(window.get("views") or 0.0),
            "pipeline_total": float(window.get("pipelines") or 0.0),
            "images_total": float(window.get("images") or 0.0),
            "arena_peak_mb": float(window.get("arena_peak_mb") or 0.0),
            "shader_slow_count": len(slow),
            "shader_slow_ms": round(sum(s["total_ms"] for s in slow), 2),
            "shader_loaded_count": len(shaders),
            "shader_loaded_ms": round(sum(s["ms"] for s in shaders), 2),
            "pipelines_new": float(pipeline.get("pipelines_new") or 0.0),
            "pipeline_created": float(pipeline.get("created") or 0.0),
            "pipeline_create_ms": float(pipeline.get("create_ms") or 0.0),
            "pipeline_create_max_ms": float(pipeline.get("create_max_ms") or 0.0),
            "warm_hits": float(pipeline.get("warm_hits") or 0.0),
            "warm_built": float(pipeline.get("warm_built") or 0.0),
            "warm_ms": float(pipeline.get("warm_ms") or 0.0),
            "programs": kinds.get("program", 0),
            "memory_pools": kinds.get("memory_pool", 0),
            "textures": kinds.get("texture", 0),
            "excess_ms": round(wall - steady, 1),
        }
        per_window.append(entry)
    # The transition clusters: the runs of windows that are above the steady state, with the frames they cover.
    clusters = []
    for entry in per_window:
        if entry["excess_ms"] > 50.0 or entry["csv_gaps_over_100ms"]:
            if clusters and entry["window"] == clusters[-1]["windows"][-1] + 1:
                clusters[-1]["windows"].append(entry["window"])
            else:
                clusters.append({"windows": [entry["window"]]})
    out_clusters = []
    for cluster in clusters:
        members = [e for e in per_window if e["window"] in cluster["windows"]]
        first = members[0]["frames"][0]
        last = members[-1]["frames"][1]
        stalls = [(f, g) for f, g in sorted(gaps.items()) if first <= f <= last and g > STALL_MS]
        wall = sum(e["wall_ms"] for e in members)
        steady = len(members) * WINDOW_FRAMES * STEADY_FRAME_MS
        excess = wall - steady
        shader_ms = sum(e["shader_loaded_ms"] for e in members)
        pipeline_ms = sum(e["pipeline_create_ms"] for e in members)
        out_clusters.append({
            "windows": cluster["windows"],
            "frames": [first, last],
            "wall_ms": round(wall, 1),
            "excess_ms": round(excess, 1),
            "shader_loaded_count": sum(e["shader_loaded_count"] for e in members),
            "shader_loaded_ms": round(shader_ms, 1),
            "shader_slow_count": sum(e["shader_slow_count"] for e in members),
            "shader_slow_ms": round(sum(e["shader_slow_ms"] for e in members), 1),
            "pipelines_new": sum(e["pipelines_new"] for e in members),
            "pipelines_created": sum(e["pipeline_created"] for e in members),
            "pipeline_create_ms": round(pipeline_ms, 1),
            "warm_hits": sum(e["warm_hits"] for e in members),
            "programs": sum(e["programs"] for e in members),
            "submit_cpu_ms_total": round(sum(e["submit_cpu_ms_per_frame"] * 60 for e in members), 1),
            "block_ms_total": round(sum(e["block_ms_per_frame"] * 60 for e in members), 1),
            "block_sites": {k: round(sum(e["block_sites"].get(k, 0.0) for e in members), 1) for k in
                            ("gpu_sync", "legacy_wait", "complete", "poll", "event", "ring", "arena", "other", "present")},
            "submit_cpu_ms_per_frame_mean": round(sum(e["submit_cpu_ms_per_frame"] for e in members) / len(members), 2),
            "gpu_ms_per_frame_mean": round(sum(e["gpu_ms_per_frame"] for e in members) / len(members), 2),
            "sync_rb": sum(e["sync_rb"] for e in members),
            "sync_wb": sum(e["sync_wb"] for e in members),
            "ops_per_frame_mean": round(sum(e["ops_per_frame"] for e in members) / len(members), 1),
            "stall_count": len(stalls),
            "stall_ms": round(sum(g for f, g in stalls), 1),
            "stall_max_ms": round(max([g for f, g in stalls]), 1) if stalls else 0.0,
            "stalls": [(f, round(g, 1)) for f, g in stalls],
            "other_ms": round(excess - shader_ms - pipeline_ms - sum(e["block_ms_per_frame"] * 60 for e in members), 1),
        })
    return {
        "log": os.path.abspath(log_path),
        "csv": os.path.abspath(csv_path),
        "windows": len(windows),
        "csv_frames": len(rows),
        "csv_span_s": round((rows[-1]["ns"] - rows[0]["ns"]) / 1e9, 2) if rows else 0.0,
        "slow_loads": len(slow_loads),
        "slow_load_total_ms": round(sum(s["total_ms"] for s in slow_loads), 2),
        "shader_loads": len(loaded),
        "shader_load_total_ms": round(sum(s["ms"] for s in loaded), 2),
        "shader_load_reused": sum(1 for s in loaded if s["reused"]),
        "shader_load_ids": len(set(s["id"] for s in loaded)),
        "per_window": per_window,
        "clusters": out_clusters,
    }


def report(session):
    print("log:", session["log"])
    print("csv:", session["csv"], session["windows"], "windows,", session["csv_frames"], "frames,",
          session["csv_span_s"], "s")
    print("shader loads:", session["shader_loads"], "ids:", session["shader_load_ids"],
          "reused:", session["shader_load_reused"], "total ms:", session["shader_load_total_ms"],
          "| slow loads:", session["slow_loads"], "total ms:", session["slow_load_total_ms"])
    print()
    header = ("cluster", "frames", "wall_ms", "excess_ms", "shader_ms", "pipe_create_ms", "submit_cpu_ms",
              "other_ms", "stalls", "stall_max_ms", "created", "warm_hits", "programs", "ops")
    print("%-12s %-14s %9s %9s %10s %13s %13s %9s %6s %11s %8s %9s %8s %6s" % header)
    for cluster in session["clusters"]:
        print("%-12s %-14s %9.0f %9.0f %10.1f %13.1f %13.1f %9.1f %6d %11.1f %8.0f %9.0f %8d %6.0f" % (
            "W%d-%d" % (cluster["windows"][0], cluster["windows"][-1]),
            "%d-%d" % (cluster["frames"][0], cluster["frames"][1]),
            cluster["wall_ms"], cluster["excess_ms"], cluster["shader_loaded_ms"], cluster["pipeline_create_ms"],
            cluster["submit_cpu_ms_total"], cluster["other_ms"], cluster["stall_count"], cluster["stall_max_ms"],
            cluster["pipelines_created"], cluster["warm_hits"], cluster["programs"], cluster["ops_per_frame_mean"]))


def main():
    parser = argparse.ArgumentParser(description="the scene transition cost, four ways")
    parser.add_argument("--log", required=True)
    parser.add_argument("--csv", action="append", required=True)
    parser.add_argument("--json")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()
    result = {"sessions": [analyze(args.log, csv_path) for csv_path in args.csv]}
    if args.json:
        with open(args.json, "w") as out:
            json.dump(result, out, indent=1)
        print("wrote", args.json)
    for session in result["sessions"]:
        if args.report:
            report(session)
    return 0


if __name__ == "__main__":
    sys.exit(main())