#!/usr/bin/env python3
"""Analyze recorded game presents; retain long frames and identify repeats.

Clock: CLOCK_MONOTONIC, recorded after real presentation callback succeeds.
Percentiles use nearest rank. No acceptance decision or automatic warm-up cut.
Explicit timestamps select a predeclared test phase, never a frame-time filter.
"""
import argparse
import csv
import json
import math
from pathlib import Path

FIELDS = ('frame', 'monotonic_ns', 'texture_serial', 'image_writer',
          'draws', 'dispatches', 'submissions')


def analyze(rows, label, start_ns=None, end_ns=None):
    parsed = []
    previous = None
    for line, row in enumerate(rows, 2):
        try:
            r = {key: int(row[key]) for key in FIELDS}
        except (KeyError, ValueError, TypeError) as exc:
            raise ValueError(f'invalid or missing metric at line {line}') from exc
        if any(v < 0 for v in r.values()):
            raise ValueError(f'negative metric at line {line}')
        if previous and (r['frame'] <= previous['frame'] or
                         r['monotonic_ns'] < previous['monotonic_ns']):
            raise ValueError(f'nonmonotonic frame/time at line {line}')
        previous = r
        if start_ns is not None and r['monotonic_ns'] < start_ns:
            continue
        if end_ns is not None and r['monotonic_ns'] > end_ns:
            continue
        parsed.append(r)
    intervals = [(b['monotonic_ns'] - a['monotonic_ns']) / 1e6
                 for a, b in zip(parsed, parsed[1:])]
    duration = ((parsed[-1]['monotonic_ns'] - parsed[0]['monotonic_ns']) / 1e9
                if len(parsed) > 1 else 0)
    rendered = []
    seen = set()
    unknown = 0
    for r in parsed:
        # A/B alternation of two old images is also duplication. Draws to an
        # unrelated image do not prove that this presented image changed.
        identity = tuple(r[k] for k in ('texture_serial', 'image_writer'))
        if r['image_writer'] == 0:
            unknown += 1
            continue
        if identity not in seen:
            rendered.append((identity, r))
            seen.add(identity)
    ordered = sorted(intervals)
    # Rare stalls can disappear from p99 in a long run. Keep their timestamps
    # so a player report can be matched to scene transitions and raw logs.
    worst = sorted(enumerate(intervals), key=lambda item: (-item[1], item[0]))[:20]
    worst_intervals = [{
        'frame_before': parsed[i]['frame'], 'frame_after': parsed[i + 1]['frame'],
        'start_monotonic_ns': parsed[i]['monotonic_ns'],
        'end_monotonic_ns': parsed[i + 1]['monotonic_ns'],
        'elapsed_seconds': (parsed[i + 1]['monotonic_ns'] - parsed[0]['monotonic_ns']) / 1e9,
        'interval_ms': ms,
    } for i, ms in worst]

    def percentile(p):
        return ordered[max(0, math.ceil(len(ordered) * p) - 1)] if ordered else None

    return {
        'label': label, 'clock': 'CLOCK_MONOTONIC',
        'measurement': 'after successful real game presentation callback',
        'percentile_method': 'nearest-rank', 'acceptance_decision': 'NOT_EVALUATED',
        'presents': len(parsed), 'distinct_render_work_frames': len(rendered),
        'repeated_presents': len(parsed) - len(rendered) - unknown,
        'unknown_work_presents': unknown, 'duration_seconds': duration,
        'present_fps': (len(parsed) - 1) / duration if duration else None,
        'distinct_render_work_fps': max(0, len(rendered) - 1) / duration if duration else None,
        'interval_ms': {'p50': percentile(.50), 'p95': percentile(.95),
                        'p99': percentile(.99), 'max': max(intervals) if intervals else None},
        'over_100ms': sum(v > 100 for v in intervals),
        # Descriptive thresholds only: these counts never decide acceptance.
        'intervals_over_ms': {str(limit): sum(v > limit for v in intervals)
                              for limit in (40, 50, 66.667, 100)},
        'worst_intervals': worst_intervals,
        'first_monotonic_ns': parsed[0]['monotonic_ns'] if parsed else None,
        'last_monotonic_ns': parsed[-1]['monotonic_ns'] if parsed else None,
        'rows_removed_for_frame_time': 0,
        'limits': 'Work identity is rendering evidence; scene/logic/audio speed and cold/warm phase require separate observations.',
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('--label', required=True)
    p.add_argument('--start-ns', type=int)
    p.add_argument('--end-ns', type=int)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    with args.input.open(newline='') as source:
        result = analyze(csv.DictReader(source), args.label, args.start_ns, args.end_ns)
    result['source'] = str(args.input.resolve())
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.out:
        args.out.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
