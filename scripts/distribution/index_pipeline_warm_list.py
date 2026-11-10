#!/usr/bin/env python3
"""Add a verified local pipeline-state seed to a new trusted data index.

No opaque driver cache, game data upload or source-index mutation. Native
rebuild/performance coverage must be recorded separately for each device.
"""
import argparse
import hashlib
import json
from pathlib import Path
from inspect_pipeline_warm_list import archive_keys, inspect
from native_data_bundle import ROOT


def add_seed(source, warm_list, output):
    if warm_list.is_symlink():
        raise ValueError('linked seed input refused')
    local = (ROOT / '.local').resolve()
    source, warm_list, output = source.resolve(), warm_list.resolve(), output.resolve()
    if (not source.is_relative_to(local) or not warm_list.is_relative_to(local)
            or not output.is_relative_to(local) or output.is_relative_to(local / 'baselines')
            or output.exists()):
        raise ValueError('use a new local output outside frozen baselines')
    old = json.loads((source / 'index.json').read_text())
    if old['format'] != 'hnl-native-data/1' or old['abi'] != 'arm64-v8a':
        raise ValueError('unsupported source format or ABI')
    rows = old['files']
    raw = (source / 'files.tsv').read_bytes()
    inventory = ''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
    if (raw != inventory or hashlib.sha256(raw).hexdigest() != old['index_sha256']
            or len(rows) != old['file_count'] or sum(r['bytes'] for r in rows) != old['payload_bytes']):
        raise ValueError('source index differs from inventory')
    if any(r['path'] == 'pipeline-warm-list.bin' for r in rows):
        raise ValueError('source already contains a seed; preserve it and start from the original index')
    archives = [r for r in rows if r['path'] == 'shaders.hnla']
    if len(archives) != 1 or old['required'].get('shaders.hnla') != archives[0]['sha256']:
        raise ValueError('a contract-pinned shader archive is required')
    if warm_list.stat().st_size >= 64 * 1024 * 1024:
        raise ValueError('warm list exceeds runtime size limit')
    payload = warm_list.read_bytes()
    report = inspect(payload, archive_keys(Path(archives[0]['source'])))
    if report['missing_shader_keys'] or report['records'] != report['unique_records']:
        raise ValueError('missing shaders or duplicate pipeline records')
    row = dict(path='pipeline-warm-list.bin', source=str(warm_list),
               bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    rows = sorted(rows + [row], key=lambda r: r['path'].encode('utf-16-be'))
    if len(rows) > 200000 or sum(r['bytes'] for r in rows) > 64 * 1024**3:
        raise ValueError('seeded index exceeds data contract limits')
    raw = ''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
    new = dict(old, files=rows, file_count=len(rows), payload_bytes=sum(r['bytes'] for r in rows),
               index_sha256=hashlib.sha256(raw).hexdigest(),
               required={**old['required'], row['path']: row['sha256']})
    output.mkdir(parents=True, exist_ok=False)
    (output / 'files.tsv').write_bytes(raw)
    (output / 'index.json').write_text(json.dumps(new, indent=2))
    (output / 'warm-list-inspection.json').write_text(json.dumps(report, indent=2))
    return dict(file_count=len(rows), payload_bytes=new['payload_bytes'],
                index_sha256=new['index_sha256'], seed_sha256=row['sha256'],
                warm_records=report['records'], status='LOCAL_INDEX_ONLY_NOT_GAMEPLAY_ACCEPTED')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--warm-list', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.warm_list.is_symlink():
        parser.error('linked seed input refused')
    print(json.dumps(add_seed(args.index, args.warm_list, args.out), indent=2))


if __name__ == '__main__':
    main()
