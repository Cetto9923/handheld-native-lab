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
import shlex
import shutil
import subprocess
import uuid

EXPECTED = '8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8'
PACKAGE = 'org.ffrs.diagnostic'
DESTINATION = 'files/libffrs_leaf.so'
COPY_SCRIPT = f'umask 077; set -C; cat > {DESTINATION}'
COPY_COMMAND = f'run-as {shlex.quote(PACKAGE)} sh -c {shlex.quote(COPY_SCRIPT)}'


def validate_payload(payload):
    digest = hashlib.sha256(payload).hexdigest()
    if len(payload) != 2176 or digest != EXPECTED:
        raise ValueError('FAIL_INPUT: library differs from the reviewed locally generated artifact')
    return digest


def stage_copy(serial, payload, *, copy=False, adb_path='adb', runner=subprocess.run):
    """Copy already-validated bytes, or return a plan; runner injection is for host tests."""
    if not serial.strip():
        raise ValueError('An explicit selected RG477M serial is required')
    digest = hashlib.sha256(payload).hexdigest()
    record = {'scope': 'PRIVATE_FILE_COPY_ONLY', 'serial': serial, 'sha256': digest,
              'destination': f'app-private {DESTINATION}', 'device_copy': 'NOT_RUN',
              'publication': 'DIRECT_EXCLUSIVE_CREATE_THEN_VERIFY',
              'destination_state': 'NOT_CHECKED', 'ready_for_explicit_leaf_validation': False,
              'leaf_execution': 'NOT_RUN', 'game_startup': 'NOT_RUN', 'steps': []}
    if not copy:
        return record

    def adb(label, *command, input_bytes=None, allow_nonzero=False):
        try:
            result = runner([adb_path, '-s', serial, *command], input=input_bytes,
                            capture_output=True, timeout=60)
        except (OSError, subprocess.TimeoutExpired) as failure:
            record['steps'].append({'name': label, 'exitcode': 'UNKNOWN_NOT_CONFIRMED',
                                    'error': type(failure).__name__})
            raise RuntimeError(f'{label}: {type(failure).__name__}; outcome not confirmed') from failure
        record['steps'].append({'name': label, 'exitcode': result.returncode})
        if result.returncode and not allow_nonzero:
            raise RuntimeError(f'{label}: {result.stderr.decode(errors="replace").strip()[:512]}')
        return result

    def test(label, predicate):
        result = adb(label, 'shell', 'run-as', PACKAGE, 'test', predicate, DESTINATION,
                     allow_nonzero=True)
        if result.returncode not in (0, 1):
            raise RuntimeError(f'{label}: cannot establish destination state')
        return result.returncode == 0

    def readback(prefix):
        size = adb(f'{prefix}-size', 'exec-out', 'run-as', PACKAGE, 'stat', '-c', '%s', DESTINATION)
        if size.stdout.strip() != str(len(payload)).encode():
            raise RuntimeError('Private file size differs from the reviewed input')
        actual = adb(f'{prefix}-readback', 'exec-out', 'run-as', PACKAGE, 'cat', DESTINATION).stdout
        record['readback_bytes'] = len(actual)
        record['readback_sha256'] = hashlib.sha256(actual).hexdigest()
        if len(actual) != len(payload) or record['readback_sha256'] != digest:
            raise RuntimeError('Private file SHA256/length readback mismatch')

    write_attempted = False
    try:
        if adb('device-state', 'get-state').stdout.strip() != b'device':
            raise RuntimeError('Selected serial is not an online device')
        model = adb('device-model', 'shell', 'getprop', 'ro.product.model').stdout.decode().strip()
        record['device_model'] = model
        if 'rg477m' not in re.sub('[^a-z0-9]', '', model.lower()):
            raise RuntimeError('Selected serial is not identified as the requested RG477M')
        adb('debug-app-access', 'shell', 'run-as', PACKAGE, 'pwd')
        # A dangling symlink must count as an existing destination, too.
        if test('destination-symlink-check', '-L'):
            record['destination_state'] = 'EXISTING_SYMLINK_UNCHANGED'
            raise RuntimeError('Destination is a symlink; refusing to follow or replace it')
        if test('destination-check', '-e'):
            record['destination_state'] = 'EXISTING_UNCHANGED'
            if not test('destination-regular-file-check', '-f'):
                raise RuntimeError('Destination exists and is not a regular file')
            readback('existing')
            record['device_copy'] = 'ALREADY_PRESENT_VERIFIED'
            record['ready_for_explicit_leaf_validation'] = True
            return record
        # Noclobber creates the final private name exclusively; no hard link,
        # rename, deletion or system-policy change is needed. This is NOT an
        # atomic completed-file publication: only validate after this succeeds.
        write_attempted = True
        record['destination_state'] = 'PARTIAL_OR_UNCERTAIN_RETAINED'
        adb('private-exclusive-copy', 'shell', '-T', COPY_COMMAND, input_bytes=payload)
        readback('copy')
        adb('make-readonly', 'shell', 'run-as', PACKAGE, 'chmod', '400', DESTINATION)
        record['device_copy'] = 'COPY_COMPLETE_ONLY'
        record['destination_state'] = 'NEW_COPY_VERIFIED_READONLY'
        record['ready_for_explicit_leaf_validation'] = True
    except Exception as failure:
        record['device_copy'] = 'FAIL_STAGE'
        record['error'] = str(failure)
        if write_attempted:
            record['recovery'] = ('The destination may be partial or belong to another writer. It is retained; '
                                  'do not validate or automatically delete/overwrite it. Inspect before any retry.')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='User-selected RG477M ADB serial; never auto-selects')
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--copy', action='store_true', help='Perform the explicit private-file copy')
    parser.add_argument('--adb', default=shutil.which('adb') or 'adb')
    args = parser.parse_args()
    payload = args.library.read_bytes()
    try:
        validate_payload(payload)
        record = stage_copy(args.serial, payload, copy=args.copy, adb_path=args.adb)
    except ValueError as failure:
        parser.error(str(failure))
    record['library'] = str(args.library.resolve())
    if args.copy:
        log = Path(__file__).resolve().parents[2] / '.local/logs' / f'leaf-stage-{uuid.uuid4().hex}.json'
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))
    return 0 if record['device_copy'] in ('NOT_RUN', 'COPY_COMPLETE_ONLY', 'ALREADY_PRESENT_VERIFIED') else 1


if __name__ == '__main__':
    raise SystemExit(main())
