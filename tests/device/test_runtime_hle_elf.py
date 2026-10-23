#!/usr/bin/env python3
"""Reject malformed/mismatched static CLI evidence. Never runs Android code."""
import argparse
from pathlib import Path
import struct
import subprocess
from verify_runtime_hle_elf import verify_layout, verify_inspection

parser = argparse.ArgumentParser()
parser.add_argument('--elf', type=Path, required=True)
parser.add_argument('--readelf', type=Path, required=True)
args = parser.parse_args()
data = args.elf.read_bytes()
output = subprocess.run([str(args.readelf), '-h', '-l', '-d', '-Ws', str(args.elf)],
                        text=True, capture_output=True, check=True).stdout
assert verify_layout(data)['machine'] == 183
assert set(verify_inspection(output)['needed']) == {'libc.so', 'libm.so', 'libdl.so'}

def rejected(function, value):
    try:
        function(value)
    except ValueError:
        return
    raise AssertionError('Malformed artifact/evidence was accepted')

rejected(verify_layout, data[:63])
bad = bytearray(data); struct.pack_into('<H', bad, 18, 62)  # x86_64
rejected(verify_layout, bad)
bad = bytearray(data); struct.pack_into('<Q', bad, 24, 0)  # no executable entry
rejected(verify_layout, bad)
phoff = struct.unpack_from('<Q', data, 32)[0]
phsize, phcount = struct.unpack_from('<HH', data, 54)
for index in range(phcount):
    offset = phoff + index * phsize
    kind = struct.unpack_from('<I', data, offset)[0]
    if kind == 1:
        bad = bytearray(data); struct.pack_into('<I', bad, offset + 4, 7)  # WX
        rejected(verify_layout, bad)
        bad = bytearray(data); struct.pack_into('<Q', bad, offset + 48, 4096)
        rejected(verify_layout, bad)
    elif kind == 3:
        bad = bytearray(data)
        interpreter = struct.unpack_from('<Q', data, offset + 8)[0]
        bad[interpreter] = ord('x')
        rejected(verify_layout, bad)
    elif kind == 0x6474e551:
        bad = bytearray(data); struct.pack_into('<I', bad, offset + 4, 7)
        rejected(verify_layout, bad)
rejected(verify_inspection, output.replace('[libc.so]', '[libSystem.B.dylib]'))
rejected(verify_inspection, output.replace('nso_host_resolve', 'missing_resolver'))
rejected(verify_inspection, output.replace('nso_host_exit_scope_end', 'missing_scope_end'))
rejected(verify_inspection, output.replace('BIND_NOW', 'NOT_BOUND'))
rejected(verify_inspection, output + '\n(FLAGS) TEXTREL\n')
print('HOST_STATIC_TESTS_ONLY: real ELF parsing and architecture, entry, WX, page, linker, stack, dependency, typed resolver/cleanup and text-relocation rejection passed; Android execution NOT_RUN.')
