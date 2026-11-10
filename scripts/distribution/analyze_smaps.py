#!/usr/bin/env python3
"""Summarize Linux/Android smaps by mapping name; no device changes or uploads."""
import argparse
from collections import defaultdict
import json
import re
from pathlib import Path

HEADER = re.compile(r'^([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+([rwxps-]{4})\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+:[0-9a-fA-F]+\s+\d+(?:\s+(.*))?$')
VALUE = re.compile(r'^([A-Za-z_]+):\s+(\d+)\s+kB\s*$')
FIELDS = ('Size', 'Rss', 'Pss', 'Private_Clean', 'Private_Dirty', 'Swap', 'SwapPss')
REQUIRED = FIELDS


def summarize(path):
    groups = defaultdict(lambda: {'mappings': 0, **{field: 0 for field in FIELDS}})
    current = None
    values = {}
    count = 0

    def finish():
        nonlocal count
        if current is None:
            return
        missing = [field for field in REQUIRED if field not in values]
        if missing:
            raise ValueError('incomplete mapping; missing ' + ','.join(missing))
        row = groups[current]
        row['mappings'] += 1
        for field in FIELDS:
            row[field] += values.get(field, 0)
        count += 1

    with Path(path).open(encoding='utf-8', errors='strict') as source:
        for raw in source:
            line = raw.rstrip('\n')
            h = HEADER.match(line)
            if h:
                finish()
                if int(h.group(1), 16) >= int(h.group(2), 16):
                    raise ValueError('invalid mapping range')
                current = h.group(4) or '[anonymous]'
                values = {}
                continue
            m = VALUE.match(line)
            if m and m.group(1) in FIELDS:
                if current is None or m.group(1) in values:
                    raise ValueError('unexpected or duplicate mapping metric')
                values[m.group(1)] = int(m.group(2))
    finish()
    if not count:
        raise ValueError('no readable smaps mappings')
    return dict(groups)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path, nargs='?')
    args = parser.parse_args()
    before = summarize(args.before)
    after = summarize(args.after) if args.after else None
    names = sorted(set(before) | (set(after) if after else set()))
    result = []
    for name in names:
        old = before.get(name, {})
        new = after.get(name, {}) if after else None
        row = {'mapping': name, 'before_kb': old}
        if after is not None:
            row['after_kb'] = new
            row['delta_kb'] = {field: new.get(field, 0) - old.get(field, 0) for field in FIELDS}
            row['mapping_delta'] = new.get('mappings', 0) - old.get('mappings', 0)
        result.append(row)
    if after is not None:
        result.sort(key=lambda r: r['delta_kb']['Pss'] + r['delta_kb']['SwapPss'], reverse=True)
    else:
        result.sort(key=lambda r: r['before_kb']['Pss'] + r['before_kb']['SwapPss'], reverse=True)
    print(json.dumps({'unit': 'KiB', 'mapping_count_before': sum(r['mappings'] for r in before.values()),
                      'mapping_count_after': sum(r['mappings'] for r in after.values()) if after is not None else None,
                      'limitations': 'Snapshots may race mapping changes; PSS and swap are not allocation stacks or complete GPU usage.',
                      'groups': result}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
