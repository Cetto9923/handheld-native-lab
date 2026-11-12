#!/usr/bin/env python3
"""Package a private, already copied FFRS folder.

Three modes:
  * package a folder into a new portable ZIP;
  * add the learned pipeline recipes to an already packaged ZIP, without
    rewriting its payload;
  * restore such a ZIP to the state it had before the addition.

No device access and no uploads.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from inspect_pipeline_warm_list import archive_keys, archive_keys_from_file, inspect

ROOT = Path(__file__).resolve().parents[2]
# The learned pipeline recipes travel with the folder: the engine reads them
# next to main.nso and builds those pipelines before the first battle instead
# of inside it. The file is a verified entity, not a loose cache.
SEED_NAME = 'pipeline-warm-list.bin'
SEED_ENTRY = 'FFRS/' + SEED_NAME
ARCHIVE_ENTRY = 'FFRS/shaders.hnla'
REQUIRED = {'main.nso', 'sdk.nso', 'romfs', 'shaders.hnla', 'shaders'}
OPTIONAL = {SEED_NAME}


def sha256_file(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def verify_seed(seed, available_keys):
    """Check the seed entity: framing, unique records and known shader keys.

    The archive is read at its index only; the payload is never touched.
    """
    if seed.is_symlink() or not seed.is_file():
        raise ValueError('the pipeline warm seed must be a real file')
    size = seed.stat().st_size
    if size < 1 or size >= 64 * 1024 * 1024:
        raise ValueError('the pipeline warm seed is empty or oversized')
    payload = seed.read_bytes()
    report = inspect(payload, available_keys)
    if report['missing_shader_keys']:
        raise ValueError('the pipeline warm seed names unknown shaders')
    if report['records'] != report['unique_records']:
        raise ValueError('the pipeline warm seed holds duplicate records')
    return report


def seed_block(seed, archive):
    """The manifest block that describes the seed entity."""
    return {
        'path': SEED_ENTRY, 'bytes': seed.stat().st_size,
        'sha256': sha256_file(seed),
        'records': archive['records'], 'unique_records': archive['unique_records'],
        'shader_keys': archive['shader_keys'],
        'missing_shader_keys': archive['missing_shader_keys'],
        'validation': 'record framing and shader identities against the archive index only; '
                      'not GPU, cache portability or performance acceptance'}


def add_seed_to_zip(archive_path, seed_path):
    """Append the learned recipes to a packaged folder.

    The payload is not rewritten. The new entry and the central directory are
    the only bytes written; the packaged entries keep their offsets, their
    sizes and their CRC values. The finished ZIP is read back entry by entry.
    """
    if archive_path.is_symlink() or not archive_path.is_file():
        raise ValueError('the packaged folder is missing')
    if '.local' not in archive_path.parts:
        raise ValueError('the packaged folder lives in a private .local directory')
    if seed_path.is_symlink() or not seed_path.is_file():
        raise ValueError('the pipeline warm seed must be a real file')
    with zipfile.ZipFile(archive_path) as z:
        names = z.namelist()
        if SEED_ENTRY in names:
            raise ValueError('the packaged folder already carries the pipeline warm seed')
        if ARCHIVE_ENTRY not in names:
            raise ValueError('the packaged folder has no shader archive')
        with z.open(ARCHIVE_ENTRY) as handle:
            keys = archive_keys_from_file(handle, z.getinfo(ARCHIVE_ENTRY).file_size)
        before = [(i.filename, i.header_offset, i.CRC, i.file_size, i.compress_size)
                  for i in z.infolist()]
        start_dir = z.start_dir
    report = verify_seed(seed_path, keys)
    size = archive_path.stat().st_size
    with archive_path.open('rb') as f:
        f.seek(start_dir)
        tail = f.read()
    if not tail or tail[:4] != b'PK\x01\x02':
        raise ValueError('the packaged folder has no readable directory')
    # The tail (the old directory and its end record) is kept, so the folder
    # can be restored byte for byte.
    tail_path = archive_path.with_name(archive_path.name + '.tail.bak')
    tail_path.write_bytes(tail)
    before_sha = sha256_file(archive_path)
    # The recovery point is written down before the folder is touched, so a
    # half finished addition can always be undone.
    sidecar_path = archive_path.with_name(archive_path.name + '.prewarm-seed.json')
    sidecar_path.write_text(json.dumps({
        'mode': 'add-seed', 'zip': str(archive_path), 'state': 'started',
        'entries_before': len(before), 'bytes_before': size,
        'sha256_before': before_sha, 'directory_start': start_dir,
        'directory_backup': str(tail_path),
        'restore_command': 'truncate -s %d %s && cat %s >> %s'
                           % (start_dir, archive_path, tail_path, archive_path),
    }, indent=2) + '\n')
    with zipfile.ZipFile(archive_path, 'a', compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        with z.open(SEED_ENTRY, 'w', force_zip64=True) as dst, seed_path.open('rb') as src:
            for chunk in iter(lambda: src.read(4 * 1024 * 1024), b''):
                dst.write(chunk)
    with zipfile.ZipFile(archive_path) as z:
        after = [(i.filename, i.header_offset, i.CRC, i.file_size, i.compress_size)
                 for i in z.infolist()]
        if after[:len(before)] != before:
            raise ValueError('the packaged entries changed')
        if [entry[0] for entry in after[len(before):]] != [SEED_ENTRY]:
            raise ValueError('unexpected entries were added')
        if z.testzip() is not None:
            raise ValueError('the finished ZIP has a CRC error')
        h = hashlib.sha256()
        with z.open(SEED_ENTRY) as src:
            for chunk in iter(lambda: src.read(4 * 1024 * 1024), b''):
                h.update(chunk)
        if h.hexdigest() != report['sha256']:
            raise ValueError('the seed payload in the ZIP does not match the seed')
    after_size = archive_path.stat().st_size
    after_sha = sha256_file(archive_path)
    block = {
        'mode': 'add-seed', 'zip': str(archive_path),
        'entries_before': len(before), 'entries_after': len(after),
        'bytes_before': size, 'bytes_after': after_size,
        'bytes_added': after_size - size,
        'sha256_before': before_sha, 'sha256_after': after_sha,
        'directory_start': start_dir,
        'directory_backup': str(tail_path),
        'restore_command': 'truncate -s %d %s && cat %s >> %s'
                           % (start_dir, archive_path, tail_path, archive_path),
        'verification': 'every packaged entry read back and checked against its CRC; '
                        'the packaged entries keep their offsets, sizes and CRC values; '
                        'the seed entry read back and checked against its SHA-256',
        'pipeline_warm_seed': seed_block(seed_path, report)}
    sidecar_path.write_text(json.dumps(block, indent=2) + '\n')
    return block


def restore_zip(archive_path):
    """Restore a packaged folder to the state it had before the addition."""
    if archive_path.is_symlink() or not archive_path.is_file():
        raise ValueError('the packaged folder is missing')
    sidecar = archive_path.with_name(archive_path.name + '.prewarm-seed.json')
    tail_path = archive_path.with_name(archive_path.name + '.tail.bak')
    if not sidecar.is_file() or not tail_path.is_file():
        raise ValueError('there is no recorded addition to restore')
    block = json.loads(sidecar.read_text())
    with archive_path.open('r+b') as f:
        f.truncate(block['directory_start'])
        f.seek(block['directory_start'])
        f.write(tail_path.read_bytes())
    digest = sha256_file(archive_path)
    if digest != block['sha256_before']:
        raise ValueError('the restored folder does not match the recorded digest')
    return {'mode': 'restore', 'zip': str(archive_path),
            'sha256': digest, 'bytes': archive_path.stat().st_size,
            'restored_to': 'the state before the pipeline warm seed was added'}


def delivery_seed_report(zip_path):
    """Read the seed and the shader archive out of a packaged folder."""
    with zipfile.ZipFile(zip_path) as z:
        if SEED_ENTRY not in z.namelist():
            raise ValueError('the packaged folder does not carry the pipeline warm seed')
        with z.open(ARCHIVE_ENTRY) as handle:
            keys = archive_keys_from_file(handle, z.getinfo(ARCHIVE_ENTRY).file_size)
        payload = z.read(SEED_ENTRY)
    report = inspect(payload, keys)
    if report['missing_shader_keys']:
        raise ValueError('the packaged seed names unknown shaders')
    return report, payload


def refresh_delivery(directory, zip_path):
    """Bring the delivery checksums and manifest in line with the folder.

    The delivery folder holds the SHA-256 of the packaged folder and the
    manifest of its files. After the learned recipes were added, both have to
    name the new folder, or the team verifies a file that no longer exists.
    """
    manifest_path = directory / 'data-manifest.json'
    if not manifest_path.is_file():
        raise ValueError('the delivery folder has no manifest')
    report, payload = delivery_seed_report(zip_path)
    manifest = json.loads(manifest_path.read_text())
    rows = [row for row in manifest.get('files', []) if row.get('path') != SEED_ENTRY]
    rows.append({'path': SEED_ENTRY, 'bytes': len(payload),
                 'sha256': hashlib.sha256(payload).hexdigest()})
    manifest['files'] = rows
    manifest['file_count'] = len(rows)
    manifest['payload_bytes'] = sum(row['bytes'] for row in rows)
    manifest['zip_bytes'] = zip_path.stat().st_size
    manifest['zip_sha256'] = sha256_file(zip_path)
    manifest['pipeline_warm_seed'] = {
        'path': SEED_ENTRY, 'bytes': len(payload),
        'sha256': hashlib.sha256(payload).hexdigest(),
        'records': report['records'], 'unique_records': report['unique_records'],
        'shader_keys': report['shader_keys'],
        'missing_shader_keys': report['missing_shader_keys'],
        'validation': 'record framing and shader identities against the archive index only; '
                      'not GPU, cache portability or performance acceptance'}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    (directory / 'FFRS-data.zip.sha256').write_text(manifest['zip_sha256'] + '  FFRS-data.zip\n')
    return {'mode': 'refresh-delivery', 'delivery': str(directory), 'zip': str(zip_path),
            'file_count': manifest['file_count'], 'zip_bytes': manifest['zip_bytes'],
            'zip_sha256': manifest['zip_sha256'],
            'pipeline_warm_seed': manifest['pipeline_warm_seed']}


def package_folder(data, out):
    """Package a folder into a new portable ZIP and write its manifest."""
    local = (ROOT / '.local').resolve()
    if not data.is_relative_to(local) or not out.is_relative_to(local) or out.exists():
        raise ValueError('use a private .local input and a new output')
    if out.is_relative_to(data):
        raise ValueError('the output may not sit inside the input')
    present = {p.name for p in data.iterdir()}
    if not REQUIRED <= present or present - (REQUIRED | OPTIONAL):
        raise ValueError('expected only main.nso, sdk.nso, romfs, archive, shader fixes '
                         'and an optional ' + SEED_NAME + '; no saves/caches')
    files = sorted(p for p in data.rglob('*') if p.is_file())
    if any(p.is_symlink() for p in data.rglob('*')):
        raise ValueError('linked inputs are not allowed')
    if any(not (data / p).is_file() for p in ['main.nso', 'sdk.nso', 'shaders.hnla']):
        raise ValueError('missing runtime file')
    if not any(p.is_file() for p in (data / 'romfs').rglob('*')):
        raise ValueError('empty romfs')
    seed_report = None
    if (data / SEED_NAME).exists():
        seed_report = verify_seed(data / SEED_NAME, archive_keys(data / 'shaders.hnla'))
    out.mkdir(parents=True, exist_ok=False)
    rows = []
    archive = out / 'FFRS-data.zip'
    # Game Paks and SPIR-V are already dense. STORE avoids another expensive
    # compressor and preserves ZIP64 support for files larger than 4 GiB.
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        for index, path in enumerate(files):
            name = 'FFRS/' + path.relative_to(data).as_posix()
            h = hashlib.sha256()
            before = path.stat()
            with path.open('rb') as src, z.open(name, 'w', force_zip64=True) as dst:
                for chunk in iter(lambda: src.read(4 * 1024 * 1024), b''):
                    h.update(chunk)
                    dst.write(chunk)
            after = path.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError('input changed while packaging: ' + name)
            rows.append({'path': name, 'bytes': before.st_size, 'sha256': h.hexdigest()})
            print(f'packed {index + 1}/{len(files)} {name}', flush=True)
    # Independently read the finished ZIP, checking CRC and every payload SHA.
    with zipfile.ZipFile(archive) as z:
        if z.namelist() != [r['path'] for r in rows]:
            raise ValueError('ZIP inventory mismatch')
        for row in rows:
            h = hashlib.sha256()
            with z.open(row['path']) as src:
                for chunk in iter(lambda: src.read(4 * 1024 * 1024), b''):
                    h.update(chunk)
            if h.hexdigest() != row['sha256']:
                raise ValueError('ZIP payload mismatch: ' + row['path'])
    h = hashlib.sha256()
    with archive.open('rb') as src:
        for chunk in iter(lambda: src.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    manifest = {'format': 'ffrs-portable-folder-v1', 'file_count': len(rows),
                'payload_bytes': sum(r['bytes'] for r in rows), 'files': rows,
                'zip_bytes': archive.stat().st_size, 'zip_sha256': h.hexdigest(),
                'verification': 'finished ZIP CRC and all payload SHA256 read back',
                'scope': 'private local copy; no saves, signing keys, upload or phone acceptance'}
    if seed_report is not None:
        manifest['pipeline_warm_seed'] = seed_block(data / SEED_NAME, seed_report)
    (out / 'data-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out / 'FFRS-data.zip.sha256').write_text(h.hexdigest() + '  FFRS-data.zip\n')
    print(json.dumps({k: v for k, v in manifest.items() if k != 'files'}, indent=2))
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--add-to', type=Path, dest='add_to')
    ap.add_argument('--seed', type=Path)
    ap.add_argument('--restore', type=Path)
    ap.add_argument('--refresh-delivery', type=Path, dest='refresh_delivery')
    ap.add_argument('--delivery', type=Path)
    a = ap.parse_args()
    if a.restore:
        print(json.dumps(restore_zip(a.restore.absolute()), indent=2))
        return
    if a.refresh_delivery:
        if a.add_to is None:
            ap.error('--refresh-delivery needs --add-to to name the packaged folder')
        print(json.dumps(refresh_delivery(a.refresh_delivery.absolute(),
                                          a.add_to.absolute()), indent=2))
        return
    if a.add_to:
        seed = a.seed if a.seed else (a.data / SEED_NAME if a.data else None)
        if seed is None:
            ap.error('--add-to needs --seed or a --data folder that holds the seed')
        block = add_seed_to_zip(a.add_to.absolute(), seed.absolute())
        print(json.dumps(block, indent=2))
        if a.delivery:
            print(json.dumps(refresh_delivery(a.delivery.absolute(), a.add_to.absolute()),
                             indent=2))
        return
    if a.data is None or a.out is None:
        ap.error('--data and --out are required to package a folder')
    local = (ROOT / '.local').resolve()
    data, out = a.data.resolve(), a.out.resolve()
    if (a.data.is_symlink() or not data.is_relative_to(local)
            or not out.is_relative_to(local) or out.exists()
            or out.is_relative_to(data) or out.is_relative_to(local / 'baselines')):
        ap.error('use private .local input and a new output outside data/baselines')
    package_folder(data, out)


if __name__ == '__main__':
    main()
