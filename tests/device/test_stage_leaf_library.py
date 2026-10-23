#!/usr/bin/env python3
"""Host-only staging contract tests: synthetic bytes, fake ADB, private temp dirs."""
import hashlib
import importlib.util
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('stage_leaf', root / 'scripts/device/stage_leaf_library.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)
PAYLOAD = b'HOST_SYNTHETIC_STAGING_TEST_ONLY' * 8
SERIAL = 'explicit-selected-rg477m'


class FakeADB:
    def __init__(self, directory, *, model='Anbernic RG477M', failure=None):
        self.directory = directory
        self.target = directory / stage.DESTINATION
        self.target.parent.mkdir()
        self.model, self.failure, self.calls = model, failure, []

    def __call__(self, argv, *, input=None, capture_output, timeout):
        assert argv[:3] == ['fake-adb', '-s', SERIAL]
        assert capture_output and timeout == 60
        self.calls.append(argv[3:])
        command = argv[3:]
        def result(code=0, output=b'', error=b''):
            return subprocess.CompletedProcess(argv, code, output, error)
        if command == ['get-state']:
            return result(output=b'device\n')
        if command == ['shell', 'getprop', 'ro.product.model']:
            return result(output=self.model.encode() + b'\n')
        if command == ['shell', 'run-as', stage.PACKAGE, 'pwd']:
            return result(output=b'/data/user/0/org.ffrs.diagnostic\n')
        if command[:4] == ['shell', 'run-as', stage.PACKAGE, 'test']:
            assert command[5] == stage.DESTINATION
            if self.failure == 'state-unknown':
                return result(2, error=b'Permission denied')
            present = {'-L': self.target.is_symlink(), '-e': self.target.exists(),
                       '-f': self.target.is_file()}[command[4]]
            return result(0 if present else 1)
        if command == ['shell', '-T', stage.COPY_COMMAND]:
            if self.failure == 'race':
                self.target.write_bytes(b'OTHER_WRITER_RETAINED')
            if self.failure in ('partial', 'timeout'):
                with self.target.open('xb') as output:
                    output.write(input[:5])
                if self.failure == 'timeout':
                    raise subprocess.TimeoutExpired(argv, 60)
                return result(1, error=b'Simulated transport interruption')
            # Exercise the actual POSIX no-clobber operation on a host-only
            # temporary path. No Android or ADB process is ever started.
            return subprocess.run(['sh', '-c', stage.COPY_SCRIPT],
                                  input=input, capture_output=True, cwd=self.directory)
        if command == ['exec-out', 'run-as', stage.PACKAGE, 'stat', '-c', '%s', stage.DESTINATION]:
            if self.failure == 'stat-error-zero': return result(output=b'stat: Permission denied\n')
            return result(output=str(self.target.stat().st_size).encode() + b'\n')
        if command == ['exec-out', 'run-as', stage.PACKAGE, 'cat', stage.DESTINATION]:
            if self.failure == 'read-error-zero': return result(output=b'run-as: Permission denied\n')
            data = self.target.read_bytes()
            if self.failure == 'bad-readback': data = b'x' * len(data)
            return result(output=data)
        if command == ['shell', 'run-as', stage.PACKAGE, 'chmod', '400', stage.DESTINATION]:
            if self.failure == 'chmod': return result(1, error=b'Permission denied')
            self.target.chmod(0o400)
            return result()
        raise AssertionError(f'Unexpected fake-device command: {command}')


def copy(adb, **kwargs):
    record = stage.stage_copy(SERIAL, PAYLOAD, adb_path='fake-adb', runner=adb, **kwargs)
    assert record['leaf_execution'] == record['game_startup'] == 'NOT_RUN'
    for command in adb.calls:
        assert not {'rm', 'ln', 'mv', 'install', 'uninstall', 'pm', 'root', 'setenforce'}.intersection(command)
    return record


with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    def device(name, **kwargs):
        path = directory / name; path.mkdir()
        return FakeADB(path, **kwargs)

    adb = device('dry-run')
    assert copy(adb)['device_copy'] == 'NOT_RUN' and adb.calls == []
    adb = device('success')
    first = copy(adb, copy=True)
    assert first['device_copy'] == 'COPY_COMPLETE_ONLY' and first['ready_for_explicit_leaf_validation']
    assert adb.target.read_bytes() == PAYLOAD and stat.S_IMODE(adb.target.stat().st_mode) == 0o400
    assert first['readback_sha256'] == hashlib.sha256(PAYLOAD).hexdigest()
    original = (adb.target.stat().st_ino, adb.target.stat().st_mtime_ns, adb.target.stat().st_mode)
    second = copy(adb, copy=True)
    assert second['device_copy'] == 'ALREADY_PRESENT_VERIFIED'
    assert original == (adb.target.stat().st_ino, adb.target.stat().st_mtime_ns, adb.target.stat().st_mode)
    assert sum(c[:2] == ['shell', '-T'] for c in adb.calls) == 1

    adb = device('existing-same')
    adb.target.write_bytes(PAYLOAD); adb.target.chmod(0o600)
    assert copy(adb, copy=True)['device_copy'] == 'ALREADY_PRESENT_VERIFIED'
    assert stat.S_IMODE(adb.target.stat().st_mode) == 0o600
    adb = device('existing-other')
    existing = b'OTHER_CONTENT'; adb.target.write_bytes(existing)
    assert copy(adb, copy=True)['device_copy'] == 'FAIL_STAGE'
    assert adb.target.read_bytes() == existing
    adb = device('existing-directory'); adb.target.mkdir()
    assert copy(adb, copy=True)['device_copy'] == 'FAIL_STAGE' and adb.target.is_dir()
    adb = device('existing-symlink'); adb.target.symlink_to('absent-target')
    assert copy(adb, copy=True)['device_copy'] == 'FAIL_STAGE' and adb.target.is_symlink()
    adb = device('race', failure='race')
    assert copy(adb, copy=True)['device_copy'] == 'FAIL_STAGE'
    assert adb.target.read_bytes() == b'OTHER_WRITER_RETAINED'

    for failure in ('partial', 'timeout', 'bad-readback', 'chmod', 'stat-error-zero', 'read-error-zero'):
        adb = device(failure, failure=failure)
        record = copy(adb, copy=True)
        assert record['device_copy'] == 'FAIL_STAGE' and not record['ready_for_explicit_leaf_validation']
        assert record['destination_state'] == 'PARTIAL_OR_UNCERTAIN_RETAINED' and adb.target.exists()
        assert record['recovery']
        if failure in ('partial', 'timeout'):
            assert adb.target.read_bytes() == PAYLOAD[:5]
        if failure == 'timeout':
            assert record['steps'][-1]['exitcode'] == 'UNKNOWN_NOT_CONFIRMED'
    for model in ('sdk_gphone64_arm64', 'Other Android Tablet'):
        adb = device(model, model=model)
        assert copy(adb, copy=True)['device_copy'] == 'FAIL_STAGE'
        assert not adb.target.exists() and len(adb.calls) == 2
    adb = device('state-unknown', failure='state-unknown')
    assert copy(adb, copy=True)['device_copy'] == 'FAIL_STAGE' and not adb.target.exists()
    for invalid in (b'', b'x' * 2176):
        try: stage.validate_payload(invalid)
        except ValueError: pass
        else: raise AssertionError('Unreviewed library accepted')
    try: stage.stage_copy(' ', PAYLOAD)
    except ValueError: pass
    else: raise AssertionError('Missing explicit serial accepted')
print('HOST_SYNTHETIC_STAGING_ONLY: dry-run, exclusive copy/readback/0400, idempotence, existing file/symlink/directory, race, partial/timeout/readback/chmod failure and RG477M gate tests passed; device copy/execution NOT_RUN.')
