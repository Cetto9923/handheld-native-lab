#!/usr/bin/env python3
"""Record actual host build evidence; never infer execution from an APK."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--logs', type=Path, required=True)
parser.add_argument('--artifacts', type=Path, required=True)
parser.add_argument('--overall-exit', type=int, required=True)
parser.add_argument('--kind', choices=('apk', 'runtime-hle'), default='apk')
args = parser.parse_args()
snapshots = {}
for name in ('before', 'after-compile', 'after'):
    path = args.logs / f'build-input-{name}.json'
    if path.exists():
        snapshots[name] = json.loads(path.read_text())
stable = len(snapshots) == 3 and snapshots['before'] == snapshots['after-compile'] == snapshots['after']
exits = {}
for path in sorted(args.logs.glob('*.exitcode')):
    text = path.read_text().strip()
    exits[path.stem] = int(text) if text.lstrip('-').isdigit() else text
required_steps = ('source-guard', 'apk-signature', 'apk-manifest', 'apk-structure') if args.kind == 'apk' else ('source-guard', 'runtime-hle-link', 'runtime-hle-elf')
required_artifacts = ('probe_rg477m', 'nso_diagnostic', 'libffrs_diagnostic.so', 'ffrs-diagnostic-debug.apk') if args.kind == 'apk' else ('nso_runtime_hle',)
missing_artifacts = [name for name in required_artifacts
                     if not (args.artifacts / name).is_file() or (args.artifacts / name).stat().st_size == 0]
accepted = (args.overall_exit == 0 and stable and not missing_artifacts
            and all(exits.get(n) == 0 for n in required_steps)
            and all(type(value) is int and value == 0 for value in exits.values()))
artifacts = []
for name in required_artifacts:
    path = args.artifacts / name
    if path.is_file():
        artifacts.append({'path': str(path.resolve()), 'bytes': path.stat().st_size,
                          'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          'accepted_for_this_build': accepted})
before = snapshots.get('before', {})
record = {'schema': 'ffrs.android.build-evidence.v1',
    'recorded_at_utc': datetime.now(timezone.utc).isoformat(),
    'scope': 'HOST_STATIC_CROSS_BUILD_AND_PACKAGING_ONLY' if args.kind == 'apk' else 'HOST_STATIC_ANDROID_HLE_CLI_CROSS_BUILD_ONLY',
    'source_root': str(args.source_root.resolve()), 'source_HEAD': before.get('source_HEAD'),
    'source_dirty_at_build': before.get('source_dirty'),
    'runtime_source_revision': before.get('runtime_source_revision'),
    'source_fingerprints_stable': stable, 'overall_exitcode': args.overall_exit,
    'accepted_static_build': accepted, 'step_exitcodes': exits,
    'build_kind': args.kind, 'required_artifacts_missing_or_empty': missing_artifacts,
    'input_fingerprints': before.get('files', []), 'artifacts': artifacts,
    'step_log_directory': str(args.logs.resolve()),
    'toolchain': 'Apple Clang + Rust LLD + per-member CRC-verified partial official NDK' if os.environ.get('FFRS_USE_MINIMAL_TOOLCHAIN') == '1' else 'Configured installed Android NDK',
    'whole_ndk_archive_checksum': 'NOT_VERIFIED' if os.environ.get('FFRS_USE_MINIMAL_TOOLCHAIN') == '1' else 'NOT_ASSERTED_BY_THIS_SCRIPT',
    'device_execution': 'NOT_RUN', 'leaf_behavior_on_android': 'NOT_RUN',
    'game_startup': 'NOT_RUN', 'gameplay': 'NOT_RUN'}
if args.kind == 'runtime-hle':
    record['guest_tls_sdk_binding'] = 'NOT_IMPLEMENTED'
    record['apk_integration'] = 'NOT_INCLUDED_LOAD_ONLY_APK_UNCHANGED'
payload = json.dumps(record, indent=2) + '\n'
(args.artifacts / 'build-evidence.json').write_text(payload)
(args.artifacts / 'manifest.json').write_text(json.dumps(artifacts, indent=2) + '\n')
(args.logs / 'build-evidence.json').write_text(payload)
print(json.dumps({'accepted_static_build': accepted, 'overall_exitcode': args.overall_exit,
                  'source_fingerprints_stable': stable, 'device_execution': 'NOT_RUN'}))
