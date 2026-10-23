#!/usr/bin/env python3
"""Static Android HLE CLI gate. This script never executes the inspected ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

REQUIRED_SYMBOLS = {
    'main', 'nso_load_file', 'nso_free', 'nso_call', 'nso_install_fault_trap',
    'nso_set_unload_observer', 'nso_host_resolve', 'nso_host_allocate_address_region',
    'nso_host_free_address_region', 'nso_host_initialize_mutex', 'nso_host_finalize_mutex',
    'nso_host_lock_mutex', 'nso_host_try_lock_mutex', 'nso_host_unlock_mutex',
    'nso_host_is_mutex_locked_by_current_thread', 'nso_host_atexit', 'nso_host_cxa_atexit',
    'nso_host_cxa_finalize', 'nso_host_exit', 'nso_host_immediate_exit',
    'nso_host_exit_scope_begin', 'nso_host_exit_scope_end',
    'nso_exit_registry_finalize', 'nso_exit_registry_discard',
}


def verify_layout(data):
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('Expected ELF64 little-endian current-version file')
    kind, machine, version, entry, phoff = struct.unpack_from('<HHIQQ', data, 16)
    if kind != 3 or machine != 183 or version != 1 or not entry:
        raise ValueError('Expected AArch64 ET_DYN executable with a nonzero entry')
    phsize, phcount = struct.unpack_from('<HH', data, 54)
    if phsize != 56 or not phcount or phoff + phsize * phcount > len(data):
        raise ValueError('Invalid ELF program headers')
    loads, interpreters, stack_flags = [], [], []
    for index in range(phcount):
        kind, flags, offset, address, _, filesz, memsz, alignment = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + index * phsize)
        if offset + filesz > len(data):
            raise ValueError('Program header exceeds the input file')
        if kind == 1:
            if (filesz > memsz or alignment < 16384 or alignment & (alignment - 1)
                    or offset % alignment != address % alignment or flags & 3 == 3):
                raise ValueError('LOAD must be 16 KiB compatible and never writable/executable')
            loads.append({'offset': offset, 'virtual_address': address, 'file_size': filesz,
                          'memory_size': memsz, 'flags': flags, 'alignment': alignment})
        elif kind == 3:
            interpreters.append(data[offset:offset + filesz])
        elif kind == 0x6474e551:
            stack_flags.append(flags)
    if interpreters != [b'/system/bin/linker64\0']:
        raise ValueError('Expected exactly the Android 64-bit dynamic linker')
    if not loads or not any(p['flags'] & 1 and p['virtual_address'] <= entry
                            < p['virtual_address'] + p['memory_size'] for p in loads):
        raise ValueError('Entry must be inside an executable LOAD')
    if len(stack_flags) != 1 or stack_flags[0] & 1:
        raise ValueError('Expected a nonexecutable GNU_STACK')
    return {'elf_class': 64, 'machine': 183, 'abi': 'arm64-v8a', 'type': 'ET_DYN',
            'entry': entry, 'interpreter': '/system/bin/linker64', 'load_segments': loads}


def verify_inspection(output):
    needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', output)
    if not needed or not set(needed).issubset({'libc.so', 'libm.so', 'libdl.so'}) or 'libc.so' not in needed:
        raise ValueError(f'Unexpected Android dynamic dependencies: {needed}')
    if 'TEXTREL' in output:
        raise ValueError('Text relocations are forbidden')
    if 'BIND_NOW' not in output or 'GNU_RELRO' not in output:
        raise ValueError('Expected immediate binding and GNU RELRO')
    defined, undefined = set(), set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 8 and re.fullmatch(r'\d+:', fields[0]):
            name = fields[7].split('@', 1)[0]
            (undefined if fields[6] == 'UND' else defined).add(name)
    missing = REQUIRED_SYMBOLS - defined
    if missing:
        raise ValueError(f'Missing typed HLE/runtime definitions: {sorted(missing)}')
    if not {'abort', 'exit', '_Exit'}.issubset(undefined):
        raise ValueError('Fatal abort and real process exit dependencies are required')
    return {'needed': needed, 'required_symbols_defined': sorted(REQUIRED_SYMBOLS),
            'undefined_symbols': sorted(undefined)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--readelf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = args.elf.read_bytes()
    layout = verify_layout(data)
    inspection = subprocess.run([str(args.readelf), '-h', '-l', '-d', '-Ws', str(args.elf)],
                                capture_output=True, text=True, check=True)
    args.output.with_suffix('.readelf.txt').write_text(inspection.stdout)
    symbols = verify_inspection(inspection.stdout)
    record = {'status': 'PASS_STATIC_ANDROID_HLE_CLI_ONLY', 'artifact': str(args.elf.resolve()),
              'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), **layout, **symbols,
              'android_execution': 'NOT_RUN', 'game_startup': 'NOT_RUN',
              'guest_tls_sdk_binding': 'NOT_IMPLEMENTED', 'jni_hle_integration': 'NOT_INCLUDED'}
    args.output.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        raise SystemExit(f'FAIL_STATIC_ANDROID_HLE_CLI: {error}')
