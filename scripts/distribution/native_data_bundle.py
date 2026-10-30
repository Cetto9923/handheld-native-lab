#!/usr/bin/env python3
"""Index and package authorized local data for the native Android importer.

Writes only to a new .local output. Never uploads data or copies saves/keys.
Index preparation can precede APK building; packing pins the built native .so.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MAX_PAYLOAD = 64 * 1024**3
REQUIRED = {'main.nso', 'sdk.nso'} | {
    'romfs/FFRS/Content/Paks/' + name for name in [
        'FFRS-Switch.pak', 'FFRS-Switch.ucas', 'FFRS-Switch.utoc', 'global.ucas', 'global.utoc']}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(4 * 1024**2), b''):
            h.update(block)
    return h.hexdigest()


def valid_path(name):
    parts = name.split('/')
    if (not name or len(name) > 1024 or any(c in name for c in '\\\t\n\r\0:')
            or any(p in ('', '.', '..') for p in parts) or name.startswith('.hnl-')
            or name == 'ready.properties' or PurePosixPath(name).is_absolute()):
        raise ValueError('unsupported data path: ' + repr(name))


def index(game, runtime, out):
    sources = {'main.nso': game / 'main.nso', 'sdk.nso': runtime / 'sdk.nso'}
    for f in (game / 'romfs').rglob('*'):
        if f.is_symlink():
            raise ValueError('resolve source links explicitly before packaging: ' + str(f))
        if f.is_file():
            # Shader output is a host build artifact, not a retail RomFS file.
            rel = f.relative_to(game).as_posix()
            if rel.startswith('romfs/shaders/'):
                continue
            sources[rel] = f
    for f in (runtime / 'shaders').glob('*.xgs'):
        if not re.fullmatch('[0-9a-f]{16}\\.xgs', f.name):
            continue
        sources['shaders/' + f.name] = f
    if not REQUIRED <= sources.keys() or not any(s.startswith('shaders/') for s in sources):
        raise ValueError('incomplete game/runtime input')
    if len(sources) > 200000:
        raise ValueError('too many data files')
    rows = []
    # Match Java String.compareTo, including supplementary Unicode filenames.
    for name, f in sorted(sources.items(), key=lambda item: item[0].encode('utf-16-be')):
        valid_path(name)
        if f.is_symlink() or not f.is_file() or f.stat().st_size < 1:
            raise ValueError('missing, empty or linked input: ' + str(f))
        rows.append({'path': name, 'source': str(f.resolve()), 'bytes': f.stat().st_size,
                     'sha256': digest(f)})
    total = sum(r['bytes'] for r in rows)
    if total > MAX_PAYLOAD:
        raise ValueError('data size limit exceeded')
    raw = ''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
    if len(raw) > 32 * 1024**2:
        raise ValueError('data index size limit exceeded')
    out.mkdir(parents=True, exist_ok=False)
    (out / 'files.tsv').write_bytes(raw)
    sha = hashlib.sha256(raw).hexdigest()
    record = {'format': 'hnl-native-data/1', 'abi': 'arm64-v8a', 'index_sha256': sha,
              'file_count': len(rows), 'payload_bytes': total,
              'required': {r['path']: r['sha256'] for r in rows if r['path'] in REQUIRED},
              'files': rows, 'scope': 'local authorized inputs only; no saves/keys or upload'}
    (out / 'index.json').write_text(json.dumps(record, indent=2))
    print(json.dumps({k: v for k, v in record.items() if k != 'files'}, indent=2))


def pack(prepared, apk, output):
    record = json.loads((prepared / 'index.json').read_text())
    raw_index = (prepared / 'files.tsv').read_bytes()
    if hashlib.sha256(raw_index).hexdigest() != record['index_sha256']:
        raise ValueError('prepared index changed')
    with zipfile.ZipFile(apk) as z:
        native_sha = hashlib.sha256(z.read('lib/arm64-v8a/libffrs_game_window.so')).hexdigest()
    bundle_id = hashlib.sha256((record['index_sha256'] + '\n' + native_sha).encode()).hexdigest()
    meta = {'format': 'hnl-native-data/1', 'abi': 'arm64-v8a', 'bundle_id': bundle_id,
            'native_sha256': native_sha, 'index_sha256': record['index_sha256'],
            'file_count': record['file_count'], 'payload_bytes': record['payload_bytes']}
    if output.exists():
        raise ValueError('preserve existing package and choose a new output')
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_name(output.name + '.partial')
    if tmp.exists():
        raise ValueError('partial output already exists; inspect before retrying')
    with zipfile.ZipFile(tmp, 'x', allowZip64=True) as z:
        z.writestr('bundle.properties', ''.join(f'{k}={v}\n' for k, v in meta.items()))
        z.writestr('files.tsv', raw_index, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
        for row in record['files']:
            source = Path(row['source'])
            h = hashlib.sha256()
            copied = 0
            entry = zipfile.ZipInfo('payload/' + row['path'])
            # Paks/UCAS are already compressed; keep them streaming and cheap.
            entry.compress_type = zipfile.ZIP_DEFLATED if row['path'].startswith('shaders/') else zipfile.ZIP_STORED
            entry._compresslevel = 1
            with source.open('rb') as f, z.open(entry, 'w', force_zip64=True) as dest:
                for b in iter(lambda: f.read(1024**2), b''):
                    dest.write(b); h.update(b); copied += len(b)
            if copied != row['bytes'] or h.hexdigest() != row['sha256']:
                raise ValueError('source changed while packing: ' + row['path'])
    tmp.rename(output)
    meta.update(package_sha256=digest(output), package_bytes=output.stat().st_size,
                apk_sha256=digest(apk), status='PACKED_NOT_DEVICE_ACCEPTED')
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(meta, indent=2))
    print(json.dumps(meta, indent=2))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='action', required=True)
    p = sub.add_parser('index')
    p.add_argument('--game-root', type=Path, required=True)
    p.add_argument('--runtime-root', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p = sub.add_parser('pack')
    p.add_argument('--index', type=Path, required=True)
    p.add_argument('--apk', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    output = a.out.resolve()
    if not output.is_relative_to(ROOT / '.local') or output.is_relative_to(ROOT / '.local/baselines'):
        ap.error('output must be under this worktree .local, outside the frozen baselines')
    if a.action == 'index':
        index(a.game_root.resolve(), a.runtime_root.resolve(), output)
    else:
        pack(a.index.resolve(), a.apk.resolve(), output)


if __name__ == '__main__':
    main()
