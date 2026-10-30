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
    names = ('probe_rg477m', 'nso_diagnostic', 'libffrs_diagnostic.so', 'ffrs-diagnostic-debug.apk')
    for name in names:
        (artifacts / name).write_bytes(b'synthetic-test-artifact')
    apk = artifacts / names[-1]
    snapshot = {'source_HEAD': 'synthetic', 'source_dirty': '', 'runtime_source_revision': 'synthetic',
                'files': [{'path': 'synthetic.cpp', 'sha256': 'before'}]}
    for name in ('before', 'after-compile', 'after'):
        (logs / f'build-input-{name}.json').write_text(json.dumps(snapshot))
    for name in ('source-guard', 'apk-signature', 'apk-manifest', 'apk-structure'):
        (logs / f'{name}.exitcode').write_text('0\n')
    def record(exitcode=0, kind='apk'):
        subprocess.run([sys.executable, str(script), '--source-root', str(root), '--logs', str(logs),
                        '--artifacts', str(artifacts), '--overall-exit', str(exitcode), '--kind', kind], check=True, capture_output=True)
        return json.loads((artifacts / 'build-evidence.json').read_text())
    first = record()
    assert first['accepted_static_build'] and first['device_execution'] == 'NOT_RUN'
    assert first['leaf_behavior_on_android'] == 'NOT_RUN'
    apk.write_bytes(b'new-synthetic-artifact')
    second = record()
    assert second['artifacts'][-1]['sha256'] != first['artifacts'][-1]['sha256']
    assert second['artifacts'][-1]['sha256'] == hashlib.sha256(apk.read_bytes()).hexdigest()
    assert not record(7)['accepted_static_build']
    required_step = logs / 'apk-structure.exitcode'
    required_step.unlink()
    assert not record()['accepted_static_build']
    required_step.write_text('0\n')
    for value in ('1\n', '-7\n', 'NOT_RUN\n', '\n'):
        (logs / 'extra-compile.exitcode').write_text(value)
        assert not record()['accepted_static_build']
    (logs / 'extra-compile.exitcode').write_text('0\n')
    for name in names:
        path = artifacts / name
        previous = path.read_bytes()
        path.unlink()
        assert not record()['accepted_static_build']
        path.write_bytes(b'')
        assert not record()['accepted_static_build']
        path.write_bytes(previous)
    assert record()['accepted_static_build']
    for name in ('runtime-hle-link', 'runtime-hle-elf'):
        (logs / f'{name}.exitcode').write_text('0\n')
    assert not record(kind='runtime-hle')['accepted_static_build']
    (artifacts / 'nso_runtime_hle').write_bytes(b'synthetic-cli-not-executed')
    assert record(kind='runtime-hle')['accepted_static_build']
    snapshot['files'][0]['sha256'] = 'changed'
    (logs / 'build-input-after.json').write_text(json.dumps(snapshot))
    changed = record()
    assert not changed['accepted_static_build'] and not changed['source_fingerprints_stable']
    assert changed['device_execution'] == 'NOT_RUN'
print('HOST_SYNTHETIC_TESTS_ONLY: fresh hashing, failed/unknown/absent steps, every absent/empty required artifact, CLI scope, changed source and device-NOT_RUN rejection checks passed.')
