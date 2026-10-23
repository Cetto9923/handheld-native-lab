#!/usr/bin/env python3
"""Explicit-serial, no-overwrite staging into the diagnostic app's private files.

Default is a local plan only. --copy requires a connected RG477M and an already
installed debuggable org.ffrs.diagnostic. Never installs/uninstalls or clears data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import uuid

EXPECTED = '8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8'
PACKAGE = 'org.ffrs.diagnostic'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True, help='User-selected RG477M ADB serial; never auto-selects')
parser.add_argument('--library', type=Path, required=True)
parser.add_argument('--copy', action='store_true', help='Perform the explicit private-file copy')
parser.add_argument('--adb', default=shutil.which('adb') or 'adb')
args = parser.parse_args()
payload = args.library.read_bytes()
digest = hashlib.sha256(payload).hexdigest()
if len(payload) != 2176 or digest != EXPECTED:
    raise SystemExit('FAIL_INPUT: library differs from the reviewed locally generated artifact')
if not args.serial.strip():
    raise SystemExit('An explicit selected RG477M serial is required')
record = {'scope': 'PRIVATE_FILE_COPY_ONLY', 'serial': args.serial, 'library': str(args.library.resolve()),
          'sha256': digest, 'destination': 'app-private files/libffrs_leaf.so',
          'device_copy': 'NOT_RUN', 'leaf_execution': 'NOT_RUN', 'game_startup': 'NOT_RUN', 'steps': []}
if not args.copy:
    print(json.dumps(record, indent=2))
    raise SystemExit(0)
log = Path(__file__).resolve().parents[2] / '.local/logs' / f'leaf-stage-{uuid.uuid4().hex}.json'
temporary = f'files/.leaf-stage-{uuid.uuid4().hex}'
created = False

def adb(label, *command, input_bytes=None, allow_nonzero=False):
    result = subprocess.run([args.adb, '-s', args.serial, *command], input=input_bytes,
                            capture_output=True, timeout=60)
    record['steps'].append({'name': label, 'exitcode': result.returncode})
    if result.returncode and not allow_nonzero:
        raise RuntimeError(f'{label}: {result.stderr.decode(errors="replace").strip()[:512]}')
    return result

try:
    if adb('device-state', 'get-state').stdout.strip() != b'device':
        raise RuntimeError('Selected serial is not an online device')
    model = adb('device-model', 'shell', 'getprop', 'ro.product.model').stdout.decode().strip()
    record['device_model'] = model
    if 'rg477m' not in re.sub('[^a-z0-9]', '', model.lower()):
        raise RuntimeError('Selected serial is not identified as the requested RG477M')
    adb('debug-app-access', 'shell', 'run-as', PACKAGE, 'pwd')
    exists = adb('destination-check', 'shell', 'run-as', PACKAGE, 'test', '-e', 'files/libffrs_leaf.so', allow_nonzero=True)
    if exists.returncode == 0:
        raise RuntimeError('Destination already exists; refusing to overwrite it')
    if exists.returncode != 1:
        raise RuntimeError('Cannot establish whether the destination is absent')
    # Exclusive new temporary file; input bytes are never printed to logs.
    adb('private-copy', 'shell', '-T', f"run-as {PACKAGE} sh -c 'umask 077; set -C; cat > {temporary}'", input_bytes=payload)
    created = True
    actual = adb('copy-readback', 'exec-out', 'run-as', PACKAGE, 'cat', temporary).stdout
    if hashlib.sha256(actual).hexdigest() != digest:
        raise RuntimeError('Private copy SHA256 readback mismatch')
    adb('make-readonly', 'shell', 'run-as', PACKAGE, 'chmod', '400', temporary)
    # Hard-link publication is atomic and fails if the final name already exists.
    adb('publish-no-overwrite', 'shell', 'run-as', PACKAGE, 'ln', temporary, 'files/libffrs_leaf.so')
    record['device_copy'] = 'COPY_COMPLETE_ONLY'
except Exception as failure:
    record['device_copy'] = 'FAIL_STAGE'
    record['error'] = str(failure)
finally:
    if created:
        try:
            adb('cleanup-own-temporary', 'shell', 'run-as', PACKAGE, 'rm', temporary)
        except Exception as failure:
            record['cleanup_error'] = str(failure)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
raise SystemExit(0 if record['device_copy'] == 'COPY_COMPLETE_ONLY' else 1)
