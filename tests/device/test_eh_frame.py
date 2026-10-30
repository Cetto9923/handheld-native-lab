#!/usr/bin/env python3
"""Exercise the static unwind gate against actual ELF bytes and corrupted copies."""
import argparse
from pathlib import Path
import struct
from verify_eh_frame import verify_unwind_layout

parser = argparse.ArgumentParser()
parser.add_argument('--elf', type=Path, required=True)
parser.add_argument('--old-without-index', type=Path)
args = parser.parse_args()
data = args.elf.read_bytes()
assert verify_unwind_layout(data)['PT_GNU_EH_FRAME']


def rejected(changed):
    try:
        verify_unwind_layout(bytes(changed))
    except ValueError:
        return
    raise AssertionError('Invalid unwind metadata accepted')


phoff, shoff = struct.unpack_from('<QQ', data, 32)
phsize, phcount, shsize, shcount, strings = struct.unpack_from('<HHHHH', data, 54)
for i in range(phcount):
    offset = phoff + i * phsize
    if struct.unpack_from('<I', data, offset)[0] == 0x6474e550:
        copy = bytearray(data); struct.pack_into('<I', copy, offset, 0); rejected(copy)
        copy = bytearray(data); struct.pack_into('<Q', copy, offset + 8, len(copy) + 1); rejected(copy)
names = struct.unpack_from('<IIQQQQIIQQ', data, shoff + strings * shsize)
names_data = data[names[4]:names[4] + names[5]]
for i in range(shcount):
    offset = shoff + i * shsize
    section = struct.unpack_from('<IIQQQQIIQQ', data, offset)
    name = names_data[section[0]:].split(b'\0', 1)[0]
    if name in (b'.eh_frame', b'.eh_frame_hdr'):
        copy = bytearray(data); struct.pack_into('<Q', copy, offset + 32, 0); rejected(copy)
        copy = bytearray(data); struct.pack_into('<I', copy, offset + 4, 8); rejected(copy)
        if name == b'.eh_frame_hdr':
            copy = bytearray(data); copy[section[4]] = 0; rejected(copy)
rejected(data[:60])
if args.old_without_index:
    rejected(args.old_without_index.read_bytes())
print('STATIC_ELF_BYTES_ONLY: real indexed ELF accepted; missing/misaligned PT, missing/empty/NOBITS sections, bad header version, truncated/previous unindexed ELF rejected. Android execution NOT_RUN.')
