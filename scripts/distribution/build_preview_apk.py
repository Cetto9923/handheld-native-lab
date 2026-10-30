#!/usr/bin/env python3
"""Build an independently installable, neutrally branded installer preview.

Uses the ordinary project build with explicit local output/resource overrides.
Does not install anything, copy game input, or change the frozen baseline.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ANDROID = '{http://schemas.android.com/apk/res/android}'
PACKAGE = 'dev.handheldnativelab.preview'
ICON = '''<?xml version="1.0" encoding="utf-8"?>
<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">
 <path android:fillColor="#202433" android:pathData="M0,0h108v108h-108z" />
 <path android:fillColor="#8C9EFF" android:pathData="M22,31h10v18h14v-18h10v46h-10v-18h-14v18h-10z" />
 <path android:fillColor="#A5E2CE" android:pathData="M64,31h10v36h14v10h-24z" />
</vector>
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--version-code', type=int, default=1)
    ap.add_argument('--data-index', type=Path, help='prepared local index; only sanitized contract enters APK')
    ap.add_argument('--directory-layout', action='store_true', help='directory-first private sideload variant with explicit file-access grant')
    args = ap.parse_args()
    if args.directory_layout and not args.data_index:
        ap.error('--directory-layout requires --data-index')
    if not 1 <= args.version_code <= 2147483647:
        ap.error('invalid Android version code')
    out = args.out.resolve()
    if out.exists():
        ap.error('output already exists; preserve it and choose a new version directory')
    if not out.is_relative_to(ROOT / '.local') or out.is_relative_to(ROOT / '.local/baselines'):
        ap.error('preview output must be under this worktree .local, outside baselines')
    out.mkdir(parents=True)
    ET.register_namespace('android', ANDROID[1:-1])
    tree = ET.parse(ROOT / 'src/android/app/gamewindow/AndroidManifest.xml')
    tree.getroot().set('package', PACKAGE)
    tree.getroot().set(ANDROID + 'versionCode', str(args.version_code))
    tree.getroot().set(ANDROID + 'versionName', '0.2.0-preview.' + str(args.version_code))
    if args.data_index and not args.directory_layout:
        for permission in list(tree.getroot().findall('uses-permission')):
            tree.getroot().remove(permission)
    manifest = out / 'AndroidManifest.xml'
    tree.write(manifest, encoding='utf-8', xml_declaration=True)
    resources = out / 'res'
    (resources / 'mipmap').mkdir(parents=True)
    (resources / 'values').mkdir()
    (resources / 'mipmap/ic_launcher.xml').write_text(ICON)
    (resources / 'values/strings.xml').write_text(
        '<resources><string name="app_name">Handheld Native Lab · Preview</string></resources>\n')
    env = os.environ.copy()
    env.update(FFRS_APP_MANIFEST=str(manifest), FFRS_APP_RES=str(resources),
               FFRS_APK_DIR=str(out), FFRS_APK_NAME='handheld-native-lab-preview.apk',
               FFRS_ANDROID_LOG_DIR=str(out / 'build-logs'))
    if args.data_index:
        prepared = json.loads((args.data_index / 'index.json').read_text())
        raw_index = (args.data_index / 'files.tsv').read_bytes()
        if digest(raw_index) != prepared['index_sha256']:
            raise ValueError('prepared data index changed')
        assets = out / 'assets'
        assets.mkdir()
        contract = {k: prepared[k] for k in ['format', 'abi', 'index_sha256', 'file_count', 'payload_bytes', 'required']}
        contract['delivery'] = 'directory-v1' if args.directory_layout else 'import-v1'
        (assets / 'data-contract.json').write_text(json.dumps(contract, indent=2))
        env['FFRS_APP_ASSETS'] = str(assets)
    cmd = ['bash', 'scripts/android/build_game_window_apk.sh']
    plan = {'cwd': str(ROOT), 'command': cmd, 'package': PACKAGE,
            'controller_pid': os.getpid(), 'build_log': str(out / 'build.log'),
            'output': str(out), 'scope': 'new preview only; no device install or game-data copy',
            'environment_overrides': {k: env[k] for k in ['FFRS_APP_MANIFEST', 'FFRS_APP_RES',
                'FFRS_APK_DIR', 'FFRS_APK_NAME', 'FFRS_ANDROID_LOG_DIR'] + (['FFRS_APP_ASSETS'] if args.data_index else [])}}
    (out / 'plan.json').write_text(json.dumps(plan, indent=2))
    with (out / 'build.log').open('wb') as log:
        result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    (out / 'exit.json').write_text(json.dumps({'exit': result.returncode}))
    if result.returncode:
        return result.returncode
    apk = out / 'handheld-native-lab-preview.apk'
    with zipfile.ZipFile(apk) as z:
        native = z.read('lib/arm64-v8a/libffrs_game_window.so')
    sources = ['src/android/app/java/org/ffrs/diagnostic/GameActivity.java',
               'scripts/android/build_game_window_apk.sh',
               'src/android/native/guest_boot_jni.cpp', 'src/runtime/hle/audio_bridge.c',
               'src/android/app/java/org/ffrs/diagnostic/NativeDataBundle.java',
               'src/android/app/java/org/ffrs/diagnostic/DataBundleInstaller.java']
    record = {'package': PACKAGE, 'version_code': args.version_code,
              'apk_sha256': digest(apk.read_bytes()), 'apk_bytes': apk.stat().st_size,
              'native_sha256': digest(native),
              'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_status': subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True),
              'source_hashes': {f: digest((ROOT / f).read_bytes()) for f in sources},
              'status': 'BUILT_NOT_INSTALLED_OR_ACCEPTED',
              'branding': 'self-authored vector and neutral name; no game art'}
    (out / 'manifest.json').write_text(json.dumps(record, indent=2))
    print(json.dumps(record, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
