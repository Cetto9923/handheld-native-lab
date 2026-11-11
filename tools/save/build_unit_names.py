#!/usr/bin/env python3
"""Turn a guest memory string dump into the unit name table of the cheat dialog.

The game keeps its whole text table in memory. The dump holds every string run
of the blocks that carry Chinese text, with the address it was found at. The
localization table keeps a key next to every translated string, so a key that
is followed by a Chinese value is a pair the cheat can use.

Usage:
    python3 build_unit_names.py guest-strings.txt [-o unit-names.txt]
    python3 build_unit_names.py guest-strings.txt --pairs 40   # show the pairs
"""
import argparse
import re
import sys

CJK = re.compile('[\u3400-\u9fff\uf900-\ufaff]')
KEYISH = re.compile('^[A-Za-z0-9_./:#\\-]{2,64}$')


def parse(path):
    rows = []
    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        for line in fh:
            tab = line.find('\t')
            if tab <= 0:
                continue
            try:
                addr = int(line[:tab].strip(), 16)
            except ValueError:
                continue
            rows.append((addr, line[tab + 1:].rstrip('\n')))
    rows.sort(key=lambda r: r[0])
    return rows


def is_cjk(text):
    return bool(CJK.search(text))


def pairs(rows):
    """The key / value pairs of the text table."""
    out = []
    for i in range(len(rows) - 1):
        key, val = rows[i][1], rows[i + 1][1]
        if not is_cjk(val) or is_cjk(key):
            continue
        if not KEYISH.match(key):
            continue
        out.append((key, val, rows[i][0], rows[i + 1][0]))
    return out


def unit_id(key):
    """The unit id a key points at, when the key carries one."""
    m = re.search(r'(\\d+)', key)
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('-o', '--out', default=None)
    ap.add_argument('--pairs', type=int, default=0, help='show this many pairs')
    args = ap.parse_args()

    rows = parse(args.dump)
    print('strings: %d' % len(rows))
    found = pairs(rows)
    print('key/value pairs: %d' % len(found))

    if args.pairs:
        for key, val, a, b in found[:args.pairs]:
            print('  %-40s -> %s' % (key[:40], val[:60]))

    names = {}
    for key, val, a, b in found:
        uid = unit_id(key)
        if uid is None or not is_cjk(val):
            continue
        if len(val) > 24:
            continue
        names.setdefault(uid, val)

    print('unit ids with a name: %d' % len(names))
    for uid in sorted(names):
        print('  %-8d %s' % (uid, names[uid]))

    if args.out:
        with open(args.out, 'w', encoding='utf-8') as fh:
            for uid in sorted(names):
                fh.write('%d\t%s\n' % (uid, names[uid]))
        print('wrote %s' % args.out)


if __name__ == '__main__':
    sys.exit(main())