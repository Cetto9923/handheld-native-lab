#!/usr/bin/env python3
"""Read-only inspection of this runtime's graphics warm-list records.

Checks framing, bounded extents and shader-key identities. Does not establish
Vulkan create-info validity, GPU support, cache portability or performance.
Shader archive inspection reads its index only, not payload hashes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def module_id(key):
    # Matches warm::fnv in the project engine, including its offset basis.
    value = 1469598103934665603
    for byte in key.encode('ascii'):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value | (1 << 63)


def archive_keys(path):
    size = path.stat().st_size
    with path.open('rb') as f:
        header = f.read(16)
        if len(header) != 16:
            raise ValueError('short archive header')
        magic, count, reserved = struct.unpack('<8sII', header)
        if magic != b'HNLXGS1\0' or reserved or not 0 < count <= 1000000:
            raise ValueError('invalid archive header')
        if 16 + count * 32 > size:
            raise ValueError('archive index exceeds file')
        table = f.read(count * 32)
        if len(table) != count * 32:
            raise ValueError('truncated archive index')
    result = set()
    previous = ''
    end = 16 + count * 32
    for i in range(count):
        raw, offset, length = struct.unpack_from('<16sQQ', table, i * 32)
        key = raw.decode('ascii')
        if (not re.fullmatch('[0-9a-f]{16}', key) or key <= previous
                or offset != end or not 32 <= length <= 16 * 1024 * 1024
                or length > size - end):
            raise ValueError('invalid archive table entry')
        result.add(key)
        previous = key
        end += length
    if end != size:
        raise ValueError('archive extent mismatch')
    return result


def inspect(data, available_keys=None):
    if not data or len(data) >= 64 * 1024 * 1024:
        raise ValueError('empty or oversized warm list')
    at = 0
    records = 0
    unique = set()
    shaders = set()

    def take(size):
        nonlocal at
        if size > len(data) - at:
            raise ValueError('truncated warm record')
        value = data[at:at + size]
        at += size
        return value

    def u32():
        return struct.unpack('<I', take(4))[0]

    while at < len(data):
        if u32() != 0x57504c58:
            raise ValueError('invalid warm record magic')
        stages = u32()
        if not 1 <= stages <= 5:
            raise ValueError('invalid shader stage count')
        keys = []
        for _ in range(stages):
            length = u32()
            if length != 16:
                raise ValueError('unsupported shader key length')
            key = take(length).decode('ascii')
            if not re.fullmatch('[0-9a-f]{16}', key):
                raise ValueError('invalid shader key')
            keys.append(key)
        count = u32()
        if not 1 <= count <= 4096:
            raise ValueError('invalid pipeline word count')
        raw = take(count * 4)
        words = struct.unpack('<' + 'I' * count, raw)
        if words[0] != 1:
            raise ValueError('unsupported graphics key version')
        # Prefix: version, layout piece, render-pass piece, flags, subpass,
        # stage count, then flags/stage/module-id for each shader stage.
        pos = 1
        for _ in range(2):
            if pos >= count or words[pos] > count - pos - 1:
                raise ValueError('invalid pipeline key piece')
            pos += 1 + words[pos]
        if count - pos < 3 + stages * 4 or words[pos + 2] != stages:
            raise ValueError('pipeline stage prefix mismatch')
        pos += 3
        for key in keys:
            identity = words[pos + 2] | (words[pos + 3] << 32)
            if identity != module_id(key):
                raise ValueError('pipeline shader identity mismatch')
            pos += 4
        shaders.update(keys)
        unique.add(raw)
        records += 1
    missing = sorted(shaders - available_keys) if available_keys is not None else None
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                records=records, unique_records=len(unique), shader_keys=len(shaders),
                missing_shader_keys=missing,
                validation='record framing and shader identities only; not GPU or performance acceptance',
                archive_validation='index only, no payload SHA' if available_keys is not None else 'NOT_RUN')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('warm_list', type=Path)
    parser.add_argument('--shader-archive', type=Path)
    args = parser.parse_args()
    if args.warm_list.stat().st_size >= 64 * 1024 * 1024:
        parser.error('warm list exceeds runtime size limit')
    keys = archive_keys(args.shader_archive) if args.shader_archive else None
    result = inspect(args.warm_list.read_bytes(), keys)
    print(json.dumps(result, indent=2))
    if result['missing_shader_keys']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
