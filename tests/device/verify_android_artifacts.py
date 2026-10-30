#!/usr/bin/env python3
"""Verify real APK/ELF artifacts; this does not claim device execution."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
from verify_eh_frame import verify_unwind_layout

parser = argparse.ArgumentParser()
parser.add_argument('--apk', type=Path, required=True)
parser.add_argument('--readelf', type=Path, required=True)
args = parser.parse_args()
with zipfile.ZipFile(args.apk) as apk:
    names = apk.namelist()
    required = {'AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libffrs_diagnostic.so'}
    if not required.issubset(names):
        raise SystemExit(f'Missing APK members: {required - set(names)}')
    libraries = [n for n in names if n.endswith('.so')]
    if libraries != ['lib/arm64-v8a/libffrs_diagnostic.so']:
        raise SystemExit(f'Unexpected native libraries: {libraries}')
    if any(n.endswith(('.nso', '.nsp', '.keys', '.nca')) for n in names):
        raise SystemExit('APK must not contain private game inputs')
    dex = apk.read('classes.dex')
    if not dex.startswith(b'dex\n'):
        raise SystemExit('classes.dex is not DEX')
    native = apk.read(libraries[0])
    if native[:5] != b'\x7fELF\x02' or native[5] != 1:
        raise SystemExit('Library is not ELF64 little endian')
    if struct.unpack_from('<H', native, 18)[0] != 183:
        raise SystemExit('ELF machine is not AArch64')
    if struct.unpack_from('<H', native, 16)[0] != 3:
        raise SystemExit('JNI ELF is not ET_DYN')
    phoff = struct.unpack_from('<Q', native, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', native, 54)
    loads = []
    for index in range(phcount):
        values = struct.unpack_from('<IIQQQQQQ', native, phoff + index * phsize)
        if values[0] == 1:
            loads.append(values)
            if values[7] < 16384 or values[2] % values[7] != values[3] % values[7]:
                raise SystemExit('JNI ELF LOAD segment is not 16 KiB compatible')
            if values[1] & 3 == 3:
                raise SystemExit('JNI ELF LOAD segment is writable and executable')
    if not loads:
        raise SystemExit('JNI ELF has no LOAD segments')
    unwind = verify_unwind_layout(native)
    with tempfile.TemporaryDirectory() as temporary:
        library = Path(temporary) / 'libffrs_diagnostic.so'
        library.write_bytes(native)
        result = subprocess.run([str(args.readelf), '-h', '-l', '-SW', '-d', '-Ws', str(library)],
                                capture_output=True, text=True, check=True)
        if 'Java_org_ffrs_diagnostic_DiagnosticActivity_runNativeDiagnostic' not in result.stdout:
            raise SystemExit('JNI entry point absent from native symbol table')
        if 'Java_org_ffrs_diagnostic_DiagnosticActivity_runNativeLeafDiagnostic' not in result.stdout:
            raise SystemExit('Leaf JNI entry point absent from native symbol table')
        if 'nso_load_file' not in result.stdout:
            raise SystemExit('NSO loader absent from native symbol table')
        if 'TEXTREL' in result.stdout:
            raise SystemExit('ELF contains text relocations')
        args.apk.with_suffix('.elf-inspection.txt').write_text(result.stdout)
        for line in result.stdout.splitlines():
            if ('Machine:' in line or '(NEEDED)' in line or ' LOAD ' in line
                    or 'Java_org_ffrs_diagnostic_' in line):
                print(line)
print(json.dumps({'status': 'PASS_PACKAGING_ONLY', 'apk': str(args.apk),
                  'bytes': args.apk.stat().st_size, 'sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(),
                  'abi': 'arm64-v8a', 'unwind_index': unwind,
                  'device_execution': 'NOT_RUN', 'game_execution': 'NOT_RUN'}, indent=2))
