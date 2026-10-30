#!/usr/bin/env python3
"""The shader names of this game, computed from the game's own bytes.

Two hosts that run this game name the same shader differently:

  K_content  SHA-256(control[:nctl] || code[:ncode]), first sixteen hex
             digits. The control section and the code area are hashed
             together, so two shaders that share a control section but differ
             in their code area do not share a name. This is the name the game
             itself uses and the name the runtime computes at run time.

  K_control  SHA-256(control[:0x880]), first sixteen hex digits. The control
             section alone. This is the name the other Vulkan host uses when
             it looks up a shader bundle on disk.

Both are computed here from the same bytes of the same shader, so the two can
be compared on one sample instead of being taken on faith, and the collision
behaviour of the control-only name is measured over the real shader set
instead of assumed.

The shader set is read out of the game's own IoStore container. Everything it
writes is game derived data and stays under .local/.
"""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)

TOOLS = '.local/refs/nxnative/dm/gfx/tools'
sys.path.insert(0, TOOLS)
import uepak  # noqa: E402  (the game's own container reader, from the reference host)

GAME_PROFILE = '.local/refs/nxnative/dm/games/0100606026F12000'
UTOC = '.local/romfs/FFRS/Content/Paks/FFRS-Switch.utoc'
DECODER = '.local/shader-verify/oodle_dec'
WORK = '.local/shader-identity'

CONTROL_SIZE = 0x880
CONTROL_LENGTH_OFFSET = 0x20
CONTROL_CODE_LENGTH_OFFSET = 0x704
CODE_MAGIC = bytes.fromhex('79563412')


def control_length(control):
    """How much of the control section belongs to the name.

    A control section that states its own length is used as it is, a longer
    one only by its first 0x880 bytes.
    """
    own = struct.unpack_from('<I', control, CONTROL_LENGTH_OFFSET)[0]
    return own if 0x760 <= own < CONTROL_SIZE else CONTROL_SIZE


def content_key(control, code):
    """The game's own name for a shader."""
    nctl = control_length(control)
    ncode = struct.unpack_from('<I', control, CONTROL_CODE_LENGTH_OFFSET)[0]
    if ncode > len(code):
        ncode = len(code)
    digest = hashlib.sha256(control[:nctl] + code[:ncode]).hexdigest()
    return digest[:16], nctl, ncode


def control_key(control):
    """The other host's name for the same shader."""
    return hashlib.sha256(control[:CONTROL_SIZE]).hexdigest()[:16]


def stage_of(code, tag):
    if code[:4] == CODE_MAGIC or tag == 'SC':
        return 'cs'
    word = struct.unpack_from('<I', code, 0x30)[0]
    if (word >> 10) & 0xF == 0 and word & 0x1F == 1:
        return 'vs'
    return {1: 'vs', 2: 'hs', 3: 'ds', 4: 'gs', 5: 'ps'}.get((word >> 10) & 0xF, '?')


def open_toc():
    return uepak.Toc(UTOC)


def read_libraries(toc):
    """The shader libraries in the container, with their group tables."""
    libraries = {}
    for index, entry in enumerate(toc.ids):
        if entry[11] != 8:
            continue
        reader = uepak.R(toc.read(index, None))
        reader.take('<I')
        reader.raw(20 * reader.take('<I'))
        count = reader.take('<I')
        reader.raw(20 * count)
        gids = [reader.raw(12) for _ in range(reader.take('<I'))]
        maps = [reader.take('<II') for _ in range(reader.take('<I'))]
        total = reader.take('<I')
        ents = struct.unpack_from('<%dQ' % total, reader.b, reader.o)
        reader.o += 8 * total
        groups = [reader.take('<IIII') for _ in range(reader.take('<I'))]
        key_count = reader.take('<I')
        index_table = struct.unpack_from('<%dI' % key_count, reader.b, reader.o)
        reader.o += 4 * key_count
        libraries[index] = dict(gids=gids, groups=groups, ents=ents, index=index_table, total=total)
    return libraries


def group_bytes(toc, library, group):
    """One shader group, decompressed."""
    first, count, raw, stored = library['groups'][group]
    blob = toc.read(toc.index[library['gids'][group]], None)
    path = os.path.join(WORK, '_group.bin')
    os.makedirs(WORK, exist_ok=True)
    with open(path, 'wb') as handle:
        handle.write(blob)
    decoded = os.path.join(WORK, '_group.raw')
    subprocess.run([DECODER, path, str(raw), decoded], check=True, capture_output=True)
    with open(decoded, 'rb') as handle:
        return handle.read()


def walk_records(data):
    """Every shader record in a group, with its control section and code area."""
    offset = 0
    while offset < len(data):
        record = uepak.lslg(data[offset:])
        if record is None:
            break
        tag, control, code = record
        yield tag, control, code
        offset = data.find(b'LSLG', offset + 4)
        if offset < 0:
            break


def scan(args):
    toc = open_toc()
    libraries = read_libraries(toc)
    os.makedirs(WORK, exist_ok=True)
    report = {
        'source': UTOC,
        'profile': GAME_PROFILE,
        'content_key_rule': 'sha256(control[:nctl] || code[:ncode])[:16]',
        'control_key_rule': 'sha256(control[:0x880])[:16]',
        'libraries': [],
    }
    control_to_content = {}
    content_counts = {}
    total = 0
    for index in sorted(libraries):
        library = libraries[index]
        if args.library and index != args.library:
            continue
        entry = {'toc_index': index, 'groups': len(library['groups']), 'shaders': 0, 'stages': {}}
        for group in range(len(library['groups'])):
            if args.max_groups and group >= args.max_groups:
                break
            data = group_bytes(toc, library, group)
            for tag, control, code in walk_records(data):
                key, nctl, ncode = content_key(control, code)
                other = control_key(control)
                stage = stage_of(code, tag)
                entry['shaders'] += 1
                entry['stages'][stage] = entry['stages'].get(stage, 0) + 1
                content_counts[key] = content_counts.get(key, 0) + 1
                control_to_content.setdefault(other, set()).add(key)
                total += 1
                if args.max_shaders and entry['shaders'] > args.max_shaders:
                    break
            if args.max_shaders and entry['shaders'] > args.max_shaders:
                break
        collisions = {k: sorted(v) for k, v in control_to_content.items() if len(v) > 1}
        entry['control_key_collisions'] = len(collisions)
        report['libraries'].append(entry)
        print('library %d: %d shaders, stages %s, control-key collisions so far %d'
              % (index, entry['shaders'], entry['stages'], len(collisions)))
    report['shaders'] = total
    report['distinct_content_keys'] = len(content_counts)
    report['duplicate_content_keys'] = sum(1 for v in content_counts.values() if v > 1)
    report['distinct_control_keys'] = len(control_to_content)
    report['control_key_collisions'] = len([v for v in control_to_content.values() if len(v) > 1])
    path = os.path.join(WORK, args.out)
    with open(path, 'w') as handle:
        json.dump(report, handle, indent=1)
    print(json.dumps({k: report[k] for k in
                      ('shaders', 'distinct_content_keys', 'duplicate_content_keys',
                       'distinct_control_keys', 'control_key_collisions')}, indent=1))
    print('wrote %s' % path)
    return 0


def show(args):
    """One shader, with both names and the lengths they were taken over."""
    toc = open_toc()
    libraries = read_libraries(toc)
    for index in sorted(libraries):
        library = libraries[index]
        if args.library and index != args.library:
            continue
        for group in range(len(library['groups'])):
            data = group_bytes(toc, library, group)
            for tag, control, code in walk_records(data):
                key, nctl, ncode = content_key(control, code)
                if key != args.key:
                    continue
                sample = {
                    'content_key': key,
                    'control_key': control_key(control),
                    'stage': stage_of(code, tag),
                    'tag': tag,
                    'control_length_used': nctl,
                    'control_length_stated': struct.unpack_from('<I', control, CONTROL_LENGTH_OFFSET)[0],
                    'code_length_used': ncode,
                    'code_length_actual': len(code),
                    'control_section_size': len(control),
                }
                print(json.dumps(sample, indent=1))
                os.makedirs(WORK, exist_ok=True)
                with open(os.path.join(WORK, '%s.control.bin' % key), 'wb') as handle:
                    handle.write(control)
                with open(os.path.join(WORK, '%s.code.bin' % key), 'wb') as handle:
                    handle.write(code)
                return 0
    print('key %s not found' % args.key)
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    scan_parser = sub.add_parser('scan', help='measure both names over the real shader set')
    scan_parser.add_argument('--library', type=int, default=None, help='only this container index')
    scan_parser.add_argument('--max-groups', type=int, default=0)
    scan_parser.add_argument('--max-shaders', type=int, default=0)
    scan_parser.add_argument('--out', default='identity-report.json')
    scan_parser.set_defaults(run=scan)
    show_parser = sub.add_parser('show', help='both names of one shader')
    show_parser.add_argument('key')
    show_parser.add_argument('--library', type=int, default=None, help='only this container index')
    show_parser.set_defaults(run=show)
    args = parser.parse_args()
    return args.run(args)


if __name__ == '__main__':
    sys.exit(main())
