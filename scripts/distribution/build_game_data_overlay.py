#!/usr/bin/env python3
"""Stream a private overwrite ZIP from an explicit source-to-entry manifest."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import zipfile

from package_honor_portable import sha256_file


def archive_path(value):
    path = PurePosixPath(value)
    if (not value or any(ord(c) < 32 for c in value) or path.is_absolute() or '\\' in value or ':' in value or
            str(path) != value or any(p in ('.', '..') for p in value.split('/'))):
        raise ValueError('invalid archive path')
    allowed = (value == 'FFRS/main.nso' or value.startswith('FFRS/romfs/') or
               value in ('FFRS/dlc/2/content', 'FFRS/dlc/2.meta') or
               (len(path.parts) == 1 and path.suffix in ('.apk', '.md', '.txt')))
    if not allowed or any(p.lower() in ('save', 'saves', 'savedata', 'keys') for p in path.parts):
        raise ValueError('entry outside the data overlay')
    return value


def build(manifest_path, output):
    manifest = json.loads(manifest_path.read_text())
    if '.local' not in output.resolve().parts or output.exists() or output.is_symlink():
        raise ValueError('use a new private .local output file')
    entries = []
    names = set()
    for row in manifest['files']:
        name = archive_path(row['entry'])
        source = Path(row['source'])
        if name in names or name == 'SHA256SUMS.txt':
            raise ValueError('duplicate or reserved entry')
        if source.is_symlink() or not source.is_file():
            raise ValueError('source must be a regular file')
        names.add(name)
        entries.append((source, name))
    if not {'FFRS/main.nso', 'FFRS/dlc/2/content', 'FFRS/dlc/2.meta'} <= names:
        raise ValueError('missing module or supplied DLC')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.partial')
    records = []
    # Exclusive creation keeps a failed attempt available for diagnosis.
    with temporary.open('xb') as stream:
        with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as archive:
            for source, name in entries:
                before = source.stat()
                digest = hashlib.sha256()
                info = zipfile.ZipInfo(name)
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                with source.open('rb') as src, archive.open(info, 'w', force_zip64=True) as dst:
                    for block in iter(lambda: src.read(4 * 1024 * 1024), b''):
                        digest.update(block)
                        dst.write(block)
                after = source.stat()
                if (before.st_ino, before.st_size, before.st_mtime_ns) != (
                        after.st_ino, after.st_size, after.st_mtime_ns):
                    raise ValueError('source changed while packaging')
                records.append({'entry': name, 'bytes': before.st_size, 'sha256': digest.hexdigest()})
                print('packed', name, before.st_size, flush=True)
            archive.writestr('SHA256SUMS.txt', ''.join(
                r['sha256'] + '  ' + r['entry'] + '\n' for r in records))
    # Reading to EOF verifies zipfile's CRC and compares the full streamed SHA.
    with zipfile.ZipFile(temporary) as archive:
        if archive.namelist() != [r['entry'] for r in records] + ['SHA256SUMS.txt']:
            raise ValueError('unexpected archive entries')
        expected_sums = ''.join(r['sha256'] + '  ' + r['entry'] + '\n' for r in records).encode()
        if archive.read('SHA256SUMS.txt') != expected_sums:
            raise ValueError('archive checksum list mismatch')
        for record in records:
            digest = hashlib.sha256()
            with archive.open(record['entry']) as src:
                for block in iter(lambda: src.read(4 * 1024 * 1024), b''):
                    digest.update(block)
            if digest.hexdigest() != record['sha256']:
                raise ValueError('archive content digest mismatch')
    temporary.rename(output)
    report = {'zip': str(output.resolve()), 'bytes': output.stat().st_size,
              'sha256': sha256_file(output), 'files': records, 'crc_and_sha_readback': True}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    output.with_suffix('.sha256').write_text(report['sha256'] + '  ' + output.name + '\n')
    print('verified', report['bytes'], report['sha256'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.manifest, args.output)
