#!/usr/bin/env python3
"""Exercise evidence rejection and fresh artifact hashing with synthetic inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parents[2] / 'scripts/device/record_build_evidence.py'
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    logs = root / 'logs'; logs.mkdir()
    artifacts = root / 'artifacts'; artifacts.mkdir()
    apk = artifacts / 'ffrs-diagnostic-debug.apk'; apk.write_bytes(b'synthetic-test-artifact')
    snapshot = {'source_HEAD': 'synthetic', 'source_dirty': '', 'runtime_source_revision': 'synthetic',
                'files': [{'path': 'synthetic.cpp', 'sha256': 'before'}]}
    for name in ('before', 'after-compile', 'after'):
        (logs / f'build-input-{name}.json').write_text(json.dumps(snapshot))
    for name in ('source-guard', 'apk-signature', 'apk-manifest', 'apk-structure'):
        (logs / f'{name}.exitcode').write_text('0\n')
    def record(exitcode=0):
        subprocess.run([sys.executable, str(script), '--source-root', str(root), '--logs', str(logs),
                        '--artifacts', str(artifacts), '--overall-exit', str(exitcode)], check=True, capture_output=True)
        return json.loads((artifacts / 'build-evidence.json').read_text())
    first = record()
    assert first['accepted_static_build'] and first['device_execution'] == 'NOT_RUN'
    assert first['leaf_behavior_on_android'] == 'NOT_RUN'
    apk.write_bytes(b'new-synthetic-artifact')
    second = record()
    assert second['artifacts'][0]['sha256'] != first['artifacts'][0]['sha256']
    assert second['artifacts'][0]['sha256'] == hashlib.sha256(apk.read_bytes()).hexdigest()
    assert not record(7)['accepted_static_build']
    snapshot['files'][0]['sha256'] = 'changed'
    (logs / 'build-input-after.json').write_text(json.dumps(snapshot))
    changed = record()
    assert not changed['accepted_static_build'] and not changed['source_fingerprints_stable']
    assert changed['device_execution'] == 'NOT_RUN'
print('HOST_SYNTHETIC_TESTS_ONLY: fresh hashing, failed build, changed source and device-NOT_RUN evidence checks passed.')
