#!/usr/bin/env python3
"""Fetch official NDK ARM64 sysroot members by range; verify each ZIP CRC.

This is a partial source package, not a complete or SHA1-validated NDK install.
The published whole-archive SHA1 remains a separate full-download gate.
"""
import binascii
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / '.local/minimal-ndk'
CACHE = DEST / 'ranges'
URL = 'https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip'
SIZE = 839006233
PREFIX = 'android-ndk-r27d/toolchains/llvm/prebuilt/darwin-x86_64/'
CACHE.mkdir(parents=True, exist_ok=True)


def fetch(start, end):
    if end - start + 1 > 2 * 1024 * 1024:
        return b''.join(fetch(i, min(end, i + 2 * 1024 * 1024 - 1))
                        for i in range(start, end + 1, 2 * 1024 * 1024))
    cache = CACHE / f'{start}-{end}.bin'
    if cache.exists() and cache.stat().st_size == end - start + 1:
        return cache.read_bytes()
    for attempt in range(1, 5):
        temporary = cache.with_suffix('.partial')
        headers = cache.with_suffix('.headers')
        command = ['curl', '--fail', '--location', '--range', f'{start}-{end}',
                   '--connect-timeout', '15', '--max-time', '45', '--silent', '--show-error',
                   '--dump-header', str(headers), '--output', str(temporary), URL]
        result = subprocess.run(command, capture_output=True, text=True)
        expected = f'bytes {start}-{end}/{SIZE}'
        actual = re.findall(r'(?im)^content-range:\s*(.+?)\s*$', headers.read_text()) if headers.exists() else []
        valid = (result.returncode == 0 and temporary.exists()
                 and temporary.stat().st_size == end - start + 1 and actual and actual[-1] == expected)
        with (DEST / 'range-transfer.jsonl').open('a') as log:
            log.write(json.dumps({'start': start, 'end': end, 'attempt': attempt,
                                  'exit': result.returncode, 'valid_length_and_range': bool(valid),
                                  'stderr': result.stderr}) + '\n')
        if valid:
            temporary.replace(cache)
            return cache.read_bytes()
    raise RuntimeError(f'BLOCKED_NETWORK: official byte range {start}-{end} failed; logs preserved')


tail = fetch(SIZE - 65536, SIZE - 1)
eocd = tail.rfind(b'PK\x05\x06')
if eocd < 0:
    raise RuntimeError('ZIP end record absent')
end_record = struct.unpack_from('<4s4H2IH', tail, eocd)
count, cd_size, cd_offset = end_record[4:7]
if count != 9178 or cd_offset + cd_size > SIZE:
    raise RuntimeError('Unexpected official NDK central directory')
central = fetch(cd_offset, cd_offset + cd_size - 1)
entries = []
offset = 0
while offset < len(central):
    r = struct.unpack_from('<4s6H3I5H2I', central, offset)
    if r[0] != b'PK\x01\x02':
        raise RuntimeError('Invalid ZIP central directory signature')
    entries.append({'name': central[offset + 46:offset + 46 + r[10]].decode(), 'method': r[4],
                    'crc': r[7], 'compressed': r[8], 'size': r[9], 'offset': r[16], 'attributes': r[15]})
    offset += 46 + r[10] + r[11] + r[12]
if len(entries) != count:
    raise RuntimeError('ZIP member count mismatch')
entries.sort(key=lambda entry: entry['offset'])
for i, entry in enumerate(entries):
    entry['end'] = entries[i + 1]['offset'] if i + 1 < len(entries) else cd_offset
selected = [e for e in entries if e['name'].startswith(PREFIX) and not e['name'].endswith('/')
            and any(part in e['name'] for part in ('/sysroot/usr/include/',
                    '/sysroot/usr/lib/aarch64-linux-android/', '/lib/clang/18/include/',
                    '/lib/clang/18/lib/linux/libclang_rt.builtins-aarch64-android.a',
                    '/lib/clang/18/lib/linux/aarch64/libunwind.a',
                    '/bin/llvm-readobj', '/bin/llvm-readelf', '/bin/llvm-objcopy', '/bin/llvm-strip'))]
groups = []
for entry in selected:
    if (groups and entry['offset'] - groups[-1]['end'] <= 65536
            and entry['end'] - groups[-1]['start'] <= 2 * 1024 * 1024):
        groups[-1]['end'] = entry['end']
        groups[-1]['entries'].append(entry)
    else:
        groups.append({'start': entry['offset'], 'end': entry['end'], 'entries': [entry]})
print(f'Official partial NDK plan: {len(selected)} members, {len(groups)} ranges, '
      f'{sum(g["end"] - g["start"] for g in groups)} transfer bytes', flush=True)
manifest = []
for index, group in enumerate(groups, 1):
    data = fetch(group['start'], group['end'] - 1)
    for entry in group['entries']:
        position = entry['offset'] - group['start']
        header = struct.unpack_from('<4s5H3I2H', data, position)
        if header[0] != b'PK\x03\x04' or header[3] != entry['method']:
            raise RuntimeError('Invalid ZIP local header')
        filename_start = position + 30
        name = data[filename_start:filename_start + header[-2]].decode()
        if name != entry['name']:
            raise RuntimeError('ZIP local/central filename mismatch')
        payload_start = filename_start + header[-2] + header[-1]
        compressed = data[payload_start:payload_start + entry['compressed']]
        payload = zlib.decompress(compressed, -15) if entry['method'] == 8 else compressed
        if len(payload) != entry['size'] or (binascii.crc32(payload) & 0xffffffff) != entry['crc']:
            raise RuntimeError(f'ZIP CRC/size mismatch: {name}')
        relative = PurePosixPath(name[len(PREFIX):])
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('Unsafe ZIP path')
        path = DEST / 'toolchain' / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if (entry['attributes'] >> 16) & 0o170000 == 0o120000:
            link = PurePosixPath(payload.decode())
            if link.is_absolute() or '..' in link.parts:
                raise RuntimeError('Unsafe ZIP symlink target')
            if not path.is_symlink():
                path.symlink_to(str(link))
        else:
            path.write_bytes(payload)
            path.chmod((entry['attributes'] >> 16) & 0o777)
        manifest.append({'path': str(relative), 'bytes': len(payload), 'crc32': f'{entry["crc"]:08x}'})
    print(f'CRC-verified range {index}/{len(groups)}; extracted {len(manifest)} members', flush=True)
(DEST / 'validated-members.json').write_text(json.dumps({'source': URL,
    'validation': 'TLS origin + HTTP Content-Range + ZIP CRC32 and uncompressed size per member',
    'whole_archive_sha1_verified': False, 'members': manifest}, indent=2) + '\n')
print('Partial official Android sysroot ready; complete NDK package checksum remains pending.', flush=True)
