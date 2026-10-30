#!/usr/bin/env python3
"""Inspect Android ELF unwind index sections/program headers; never execute it."""
import argparse
import json
from pathlib import Path
import struct


def verify_unwind_layout(data):
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('Expected ELF64 little-endian input')
    if struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected AArch64 ELF')
    phoff, shoff = struct.unpack_from('<QQ', data, 32)
    phsize, phcount, shsize, shcount, strings = struct.unpack_from('<HHHHH', data, 54)
    if phsize != 56 or shsize != 64 or not phcount or not shcount or strings >= shcount:
        raise ValueError('Invalid ELF header tables')
    if phoff + phsize * phcount > len(data) or shoff + shsize * shcount > len(data):
        raise ValueError('Truncated ELF header tables')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shsize) for i in range(shcount)]
    names_section = sections[strings]
    if names_section[4] + names_section[5] > len(data):
        raise ValueError('Truncated section-name table')
    names = data[names_section[4]:names_section[4] + names_section[5]]
    required = {}
    for section in sections:
        if section[0] >= len(names):
            raise ValueError('Invalid section name')
        name = names[section[0]:].split(b'\0', 1)[0]
        if name in (b'.eh_frame', b'.eh_frame_hdr'):
            if name in required:
                raise ValueError('Duplicate unwind section')
            if section[1] != 1 or not section[2] & 2 or not section[5] or section[4] + section[5] > len(data):
                raise ValueError('Unwind sections must be nonempty allocated PROGBITS')
            required[name] = section
    if set(required) != {b'.eh_frame', b'.eh_frame_hdr'}:
        raise ValueError('Missing .eh_frame or .eh_frame_hdr')
    header = required[b'.eh_frame_hdr']
    if data[header[4]] != 1:
        raise ValueError('Unknown EH frame header version')
    segments = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize) for i in range(phcount)]
    index_segments = [p for p in segments if p[0] == 0x6474e550]
    if len(index_segments) != 1:
        raise ValueError('Expected exactly one PT_GNU_EH_FRAME')
    index = index_segments[0]
    if (index[2] != header[4] or index[3] != header[3] or index[5] < header[5]
            or index[6] < header[5] or index[2] + index[5] > len(data)):
        raise ValueError('PT_GNU_EH_FRAME does not cover its header section')
    for section in required.values():
        if not any(p[0] == 1 and p[1] & 4 and p[2] <= section[4]
                   and section[4] + section[5] <= p[2] + p[5]
                   and section[3] - p[3] == section[4] - p[2] for p in segments):
            raise ValueError('Unwind section is not mapped by a readable LOAD')
    return {'eh_frame_bytes': required[b'.eh_frame'][5], 'eh_frame_hdr_bytes': header[5],
            'PT_GNU_EH_FRAME': True, 'scope': 'STATIC_UNWIND_INDEX_ONLY_NOT_EXCEPTION_EXECUTION'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    record = {'artifact': str(args.elf.resolve()), **verify_unwind_layout(args.elf.read_bytes()),
              'android_execution': 'NOT_RUN'}
    if args.output:
        args.output.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        raise SystemExit(f'FAIL_STATIC_UNWIND_INDEX: {error}')
