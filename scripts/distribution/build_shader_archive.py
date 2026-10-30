#!/usr/bin/env python3
"""Build a local XGS archive from a previously indexed, user-supplied data set."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from native_data_bundle import ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--index', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    index = args.index.resolve()
    out = args.out.resolve()
    local = (ROOT / '.local').resolve()
    if not out.is_relative_to(local) or out.is_relative_to(local / 'baselines'):
        ap.error('output must be under .local and outside frozen baselines')
    if out.exists():
        ap.error('choose a new output; existing artifacts are preserved')
    record = json.loads((index / 'index.json').read_text())
    rows = record['files']
    inventory = ''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
    expected = (index / 'files.tsv').read_bytes()
    if inventory != expected or hashlib.sha256(expected).hexdigest() != record['index_sha256']:
        raise ValueError('input index integrity mismatch')
    selected = [r for r in rows if r['path'].startswith('shaders/')]
    if not selected or len(selected) > 1000000:
        raise ValueError('invalid archive entry count')
    records = []
    offset = 16 + 32 * len(selected)
    previous = ''
    for row in selected:
        match = re.fullmatch(r'shaders/([0-9a-f]{16})\.xgs', row['path'])
        size = row['bytes']
        if not match or match.group(1) <= previous or not isinstance(size, int) or not 32 <= size <= 16 * 1024 * 1024:
            raise ValueError('invalid shader entry ' + row['path'])
        key = match.group(1)
        source = Path(row['source'])
        if source.is_symlink() or not source.resolve().is_relative_to(local) or not source.is_file():
            raise ValueError('shader source is missing, linked, or outside .local')
        records.append((key, offset, size, row['sha256'], source))
        offset += size
        if offset > 64 * 1024**3:
            raise ValueError('archive exceeds limit')
        previous = key
    out.parent.mkdir(parents=True, exist_ok=True)
    archive_hash = hashlib.sha256()
    with out.open('xb') as dest:
        def write(data):
            dest.write(data)
            archive_hash.update(data)
        write(b'HNLXGS1\0' + struct.pack('<II', len(records), 0))
        for key, pos, size, _, _ in records:
            write(struct.pack('<16sQQ', key.encode('ascii'), pos, size))
        for i, (_, _, size, expected_sha, source) in enumerate(records):
            h = hashlib.sha256()
            n = 0
            with source.open('rb') as f:
                for chunk in iter(lambda: f.read(1024 * 1024), b''):
                    n += len(chunk)
                    if n > size:
                        raise ValueError('source length changed')
                    h.update(chunk)
                    write(chunk)
            if n != size or h.hexdigest() != expected_sha:
                raise ValueError('source integrity changed: ' + source.name)
            if i % 5000 == 0:
                print(f'verified {i+1}/{len(records)}', flush=True)
    print(json.dumps({'files': len(records), 'bytes': offset, 'sha256': archive_hash.hexdigest(),
                      'status': 'LOCAL_ARCHIVE_NOT_DEVICE_ACCEPTED'}), flush=True)


if __name__ == '__main__':
    main()
