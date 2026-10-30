#!/usr/bin/env python3
"""Cross-language package test with self-authored bytes, never game inputs."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bundle', root / 'scripts/distribution/native_data_bundle.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
out = Path(tempfile.mkdtemp(prefix='installer-interop.', dir=root / '.local'))
game, runtime = out / 'game', out / 'runtime'
for name in sorted(m.REQUIRED):
    dest = (runtime if name == 'sdk.nso' else game) / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(('Self-authored installer fixture, not game data: ' + name).encode())
shader = runtime / 'shaders/0123456789abcdef.xgs'
shader.parent.mkdir()
shader.write_bytes(b'Self-authored data, not a valid shader')
for name in ['\ue000.txt', '\U0001f680.txt']:
    (game / 'romfs' / name).write_text('Self-authored Unicode path fixture')
m.index(game, runtime, out / 'index')
apk = out / 'fixture.apk'
with zipfile.ZipFile(apk, 'w') as z:
    z.writestr('lib/arm64-v8a/libffrs_game_window.so', b'Self-authored data, not a runtime binary')
package = out / 'fixture.hnldata'
m.pack(out / 'index', apk, package)
meta = json.loads(package.with_suffix('.hnldata.json').read_text())
index = json.loads((out / 'index/index.json').read_text())
classes = out / 'classes'
subprocess.run(['javac', '--release', '8', '-d', str(classes),
                str(root / 'src/android/app/java/org/ffrs/diagnostic/NativeDataBundle.java'),
                str(root / 'tests/installer/DataBundleInterop.java')], check=True)
cmd = ['java', '-cp', str(classes), 'DataBundleInterop', str(package), str(out / 'installed'),
       meta['native_sha256'], index['index_sha256'], index['required']['main.nso'], index['required']['sdk.nso']]
subprocess.run(cmd, check=True)
active = (out / 'installed/active.properties').read_bytes()
assert b'bundle_id=' + meta['bundle_id'].encode() in active
(out / 'RESULT.json').write_text(json.dumps({'command': cmd, 'scope': 'self-authored cross-language archive only',
    'native_game_test': False, 'pass': True}, indent=2))
print('evidence=' + str(out))
