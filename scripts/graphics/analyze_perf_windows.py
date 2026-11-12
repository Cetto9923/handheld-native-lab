#!/usr/bin/env python3
"""Split the engine's 60-frame PERF windows into slow and fast groups.

The engine prints one "NVN PERF v=1 ..." line per window of 60 frames. The
window's wall_ms divided by its frame count is the average frame time of that
window, so a window that contains a long stall stands out from a window that
does not. This tool classifies the windows on that average, compares the
engine's own counters between the two groups, and reports what the window
before a slow window looked like.

Read-only. It reports what the log says; it decides nothing.
"""
import argparse
import json
from pathlib import Path
import re
import statistics

V1 = re.compile(r'NVN PERF v=1 (.*)')
PIPE = re.compile(r'NVN PERF pipelines_new=(\d+) evicted=(\d+) warm_hits=(\d+) '
                   r'created=(\d+) create_ms=([\d.]+) create_max_ms=([\d.]+) '
                   r'warm_built=(\d+) warm_failed=(\d+) warm_ms=([\d.]+)')


def field(line, name):
    m = re.search(r'(?:^| )%s=([\-0-9.]+)' % re.escape(name), line)
    return float(m.group(1)) if m else None


def parse(path):
    windows = []
    pipes = []
    for raw in Path(path).read_text(errors='replace').splitlines():
        if 'NVN PERF v=1 ' in raw:
            body = V1.search(raw).group(1)
            frames = field(body, 'frames')
            wall = field(body, 'wall_ms')
            if not frames or not wall:
                continue
            row = {'avg_frame_ms': wall / frames, 'wall_ms': wall, 'frames': frames}
            for name in ('submit_cpu_ms', 'present_cpu_ms', 'gpu_ms', 'submits', 'waits',
                         'sync_wb', 'sync_rb', 'sync_query', 'ops', 'folded', 'passes',
                         'draws', 'dispatches', 'views', 'snap_hit', 'snap_miss',
                         'host_mb', 'arena_peak_mb'):
                row[name] = field(body, name)
            m = re.search(r'why=w(\d+)/p(\d+)/a(\d+)\((\d+),(\d+),(\d+),(\d+),(\d+)\|'
                          r'(\d+),(\d+),(\d+),(\d+),(\d+)\)', body)
            if m:
                row['why_w'] = int(m.group(1))
                row['why_p'] = int(m.group(2))
                row['why_a'] = int(m.group(3))
                row['why_wait'] = int(m.group(4))
                row['why_poll'] = int(m.group(5))
                row['why_access'] = int(m.group(6))
                row['why_cmdwait'] = int(m.group(9))
                row['why_finish'] = int(m.group(10))
                row['why_slot'] = int(m.group(11))
            windows.append(row)
        elif 'NVN PERF pipelines_new=' in raw:
            m = PIPE.search(raw)
            if m:
                pipes.append({'pipelines_new': int(m.group(1)),
                              'warm_hits': int(m.group(3)),
                              'created': int(m.group(4)),
                              'create_ms': float(m.group(5)),
                              'create_max_ms': float(m.group(6)),
                              'warm_built': int(m.group(7)),
                              'warm_ms': float(m.group(9))})
    for row, pipe in zip(windows, pipes):
        row.update(pipe)
    return windows


FIELDS = ('avg_frame_ms', 'wall_ms', 'submit_cpu_ms', 'present_cpu_ms', 'gpu_ms',
          'submits', 'waits', 'sync_wb', 'sync_rb', 'sync_query', 'ops', 'folded',
          'passes', 'draws', 'dispatches', 'views', 'snap_hit', 'snap_miss',
          'pipelines_new', 'warm_hits', 'created', 'create_ms', 'create_max_ms',
          'why_w', 'why_p', 'why_a', 'why_wait', 'why_poll', 'why_access')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--slow-ms', type=float, default=50.0,
                        help='average frame time above which a window counts as slow')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    windows = parse(args.log)
    if not windows:
        raise SystemExit('no PERF windows found')
    slow = [w for w in windows if w['avg_frame_ms'] > args.slow_ms]
    fast = [w for w in windows if w['avg_frame_ms'] <= args.slow_ms]

    def stats(group):
        out = {'windows': len(group)}
        for name in FIELDS:
            values = [w[name] for w in group if w.get(name) is not None]
            if not values:
                continue
            out[name] = {'mean': round(statistics.fmean(values), 3),
                         'median': round(statistics.median(values), 3),
                         'max': round(max(values), 3)}
        return out

    # What the window before a slow window looked like.
    preceding = []
    for index, window in enumerate(windows):
        if window['avg_frame_ms'] <= args.slow_ms or index == 0:
            continue
        before = windows[index - 1]
        preceding.append({'slow_window': index,
                          'slow_avg_frame_ms': round(window['avg_frame_ms'], 1),
                          'before_sync_rb': before['sync_rb'],
                          'before_created': before['created'],
                          'before_sync_wb': before['sync_wb'],
                          'before_submit_cpu_ms': before['submit_cpu_ms'],
                          'before_gpu_ms': before['gpu_ms'],
                          'before_draws': before['draws'],
                          'before_dispatches': before['dispatches'],
                          'before_passes': before['passes'],
                          'before_why_wait': before.get('why_wait'),
                          'before_why_access': before.get('why_access'),
                          'before_why_cmdwait': before.get('why_cmdwait'),
                          'before_why_finish': before.get('why_finish'),
                          'slow_sync_rb': window['sync_rb'],
                          'slow_created': window['created'],
                          'slow_submit_cpu_ms': window['submit_cpu_ms'],
                          'slow_gpu_ms': window['gpu_ms'],
                          'slow_why_wait': window.get('why_wait'),
                          'slow_why_access': window.get('why_access'),
                          'slow_why_cmdwait': window.get('why_cmdwait'),
                          'slow_why_finish': window.get('why_finish')})

    # created and sync_rb: which comes first?
    order = {'created_window_before_rb': 0, 'same_window': 0, 'rb_window_before_created': 0,
             'neither': 0}
    for index, window in enumerate(windows):
        if window['created'] and window['sync_rb']:
            order['same_window'] += 1
        elif window['created']:
            nxt = windows[index + 1] if index + 1 < len(windows) else None
            if nxt and nxt['sync_rb'] > window['sync_rb']:
                order['created_window_before_rb'] += 1
            else:
                order['neither'] += 1
        elif window['sync_rb']:
            order['rb_window_before_created'] += 1

    report = {'log': str(args.log), 'threshold_avg_frame_ms': args.slow_ms,
              'windows_total': len(windows),
              'slow': stats(slow), 'fast': stats(fast),
              'slow_window_predecessors': preceding,
              'created_vs_sync_rb_order': order,
              'validation': 'engine PERF counters only; no acceptance decision'}
    print(json.dumps(report, indent=2))
    if args.out:
        args.out.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
