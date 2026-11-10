#!/usr/bin/env python3
"""Materialize a verified, already-extracted local APK+data folder. Never uploads."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import zipfile
from native_data_bundle import ROOT, digest, valid_path


def copy_file(source, target):
    # APFS clones preserve the frozen input and avoid another full physical copy.
    target.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == 'darwin':
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.clonefile(os.fsencode(source), os.fsencode(target), 0) == 0:
            return
    shutil.copyfile(source, target)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--index', type=Path, required=True)
    ap.add_argument('--apk', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    out = args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT / '.local') or out.is_relative_to(ROOT / '.local/baselines'):
        ap.error('choose a new .local output outside frozen baselines')
    record = json.loads((args.index / 'index.json').read_text())
    raw_index = (args.index / 'files.tsv').read_bytes()
    index_sha = hashlib.sha256(raw_index).hexdigest()
    if index_sha != record['index_sha256']:
        raise ValueError('prepared index changed')
    with zipfile.ZipFile(args.apk) as apk:
        contract = json.loads(apk.read('assets/data-contract.json'))
        native_sha = hashlib.sha256(apk.read('lib/arm64-v8a/libffrs_game_window.so')).hexdigest()
    for field in ['format', 'abi', 'index_sha256', 'file_count', 'payload_bytes', 'required']:
        if contract[field] != record[field]:
            raise ValueError('APK and directory contract do not match: ' + field)
    if contract['delivery'] != 'directory-v1':
        raise ValueError('APK is not a directory-first build')
    # Compare the source inventory with the exact trusted index, not just its own claims.
    rows = record['files']
    inventory = ''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
    if raw_index != inventory or len(rows) != record['file_count'] or sum(r['bytes'] for r in rows) != record['payload_bytes']:
        raise ValueError('private source inventory differs from trusted index')
    out.mkdir(parents=True)
    plan = {'cwd':str(ROOT), 'command':sys.argv, 'pid':os.getpid(), 'status':'RUNNING',
            'input_index_sha256':index_sha, 'scope':'authorized local inputs only; no saves, upload or source paths in delivery'}
    (out / 'export-plan.json').write_text(json.dumps(plan, indent=2))
    folder = out / 'HandheldNativeLab'
    data = folder / 'data'
    data.mkdir(parents=True)
    for at, row in enumerate(rows):
        valid_path(row['path'])
        source, target = Path(row['source']), data / row['path']
        if source.is_symlink() or not source.is_file() or source.stat().st_size != row['bytes']:
            raise ValueError('missing or changed source: ' + row['path'])
        copy_file(source, target)
        if digest(target) != row['sha256']:
            raise ValueError('export checksum mismatch: ' + row['path'])
        if at % 2000 == 0:
            print(f'verified {at + 1}/{len(rows)}', flush=True)
    bundle_id = hashlib.sha256((index_sha + '\n' + native_sha).encode()).hexdigest()
    (data / 'hnl-index.tsv').write_bytes(raw_index)
    (data / 'hnl-data.properties').write_text('bundle_id=' + bundle_id + '\nnative_sha256=' + native_sha
        + '\nindex_sha256=' + index_sha + '\npayload_bytes=' + str(record['payload_bytes']) + '\n')
    copy_file(args.apk, folder / 'handheld-native-lab-preview.apk')
    (folder / '使用说明.txt').write_text('Handheld Native Lab · 目录直读流程测试版\n\n'
        '1. 将整个 HandheldNativeLab 文件夹拷贝到手机存储或 SD 卡根目录（也支持 Games 或 Download 目录）。\n'
        '2. 点文件夹内的 APK 安装，然后打开应用。\n'
        '3. 若系统要求文件访问，点“允许读取游戏目录”并在系统设置里允许；返回后自动查找。\n'
        '4. 首次完整校验会花一些时间，请保持前台；校验通过后直接从此目录读取，不再复制数据。\n\n'
        '请完整拷贝 data 文件夹，不能混用其他版本的 SDK/NSO/Paks/shaders。\n'
        '存档与运行缓存保存在应用专属目录。升级直接覆盖安装，不要先卸载；卸载前请另做存档备份。\n'
        '本地用户授权输入，仅私有保存，禁止把游戏/生成模块/存档上传代码仓库。\n'
        '当前仅 RG477M 待实测；其他机型未验证。本包不代表完整游戏或稳定可玩验收。\n')
    plan.update(status='EXPORTED_NOT_DEVICE_ACCEPTED', bundle_id=bundle_id, native_sha256=native_sha,
                apk_sha256=digest(args.apk), file_count=len(rows), payload_bytes=record['payload_bytes'])
    (out / 'export-plan.json').write_text(json.dumps(plan, indent=2))
    print(json.dumps(plan, indent=2))


if __name__ == '__main__':
    main()
