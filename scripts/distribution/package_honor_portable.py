#!/usr/bin/env python3
"""Package a private, already copied FFRS folder. No device access or uploads."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    local = (ROOT / '.local').resolve()
    data, out = a.data.resolve(), a.out.resolve()
    if (a.data.is_symlink() or not data.is_relative_to(local)
            or not out.is_relative_to(local) or out.exists()
            or out.is_relative_to(data) or out.is_relative_to(local / 'baselines')):
        ap.error('use private .local input and a new output outside data/baselines')
    allowed = {'main.nso', 'sdk.nso', 'romfs', 'shaders.hnla', 'shaders'}
    if {p.name for p in data.iterdir()} != allowed:
        raise ValueError('expected only main.nso, sdk.nso, romfs, archive and shader fixes; no saves/caches')
    files = sorted(p for p in data.rglob('*') if p.is_file())
    if any(p.is_symlink() for p in data.rglob('*')):
        raise ValueError('linked inputs are not allowed')
    if any(not (data / p).is_file() for p in ['main.nso', 'sdk.nso', 'shaders.hnla']):
        raise ValueError('missing runtime file')
    if not any(p.is_file() for p in (data / 'romfs').rglob('*')):
        raise ValueError('empty romfs')
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
    (out / 'data-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out / 'FFRS-data.zip.sha256').write_text(h.hexdigest() + '  FFRS-data.zip\n')
    print(json.dumps({k: v for k, v in manifest.items() if k != 'files'}, indent=2))


if __name__ == '__main__':
    main()
