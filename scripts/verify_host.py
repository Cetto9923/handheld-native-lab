#!/usr/bin/env python3
"""Reproduce ordinary host checks, retaining actual process return codes.

This suite never launches a game entry, emulator, device, or downloaded code.
Its success proves only the named host checks, not Android or playable gates.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('.local/host-verification'))
    parser.add_argument('--module', type=Path, default=Path('.local/uncompressed/main.bin'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    module = args.module if args.module.is_absolute() else root / args.module
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = []

    def run(name, command, expected=0):
        start = datetime.datetime.now(datetime.timezone.utc).isoformat()
        try:
            process = subprocess.run([str(x) for x in command], cwd=root,
                                     capture_output=True, timeout=120)
            code, stdout, stderr = process.returncode, process.stdout, process.stderr
            passed = code == expected
        except subprocess.TimeoutExpired as error:
            code, stdout, stderr, passed = None, error.stdout or b'', error.stderr or b'', False
        (out / f'{name}.stdout').write_bytes(stdout)
        (out / f'{name}.stderr').write_bytes(stderr)
        result = dict(name=name, command=[str(x) for x in command],
                      started_at=start, return_code=code, expected_return_code=expected,
                      passed=passed)
        results.append(result)
        print(f'{name}: return_code={code}, expected={expected}, passed={passed}', flush=True)
        return passed

    cc = shutil.which('clang')
    cmake = shutil.which('cmake') or '/opt/homebrew/bin/cmake'
    if not cc or not Path(cmake).exists():
        parser.error('clang and CMake are required; no tools are installed by this runner')
    native = out / 'nso_call'
    graphics = out / 'test_nvn_dispatch'
    coverage = out / 'nvn_coverage'
    include = '-Isrc/graphics/nvn/include'
    common = [cc, '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
              '-pthread', include, 'src/graphics/nvn/src/nvn_dispatch.c']
    if run('nvn-build', common + ['tests/graphics/test_nvn_dispatch.c', '-o', graphics]):
        run('nvn-tests', [graphics])
    if run('nvn-coverage-build', common + ['tests/graphics/nvn_coverage.c', '-o', coverage]):
        # Missing renderer functions are an explicit failed game-stage gate.
        # Return 3 is expected for this host diagnostic, not a rendering pass.
        run('nvn-coverage', [coverage, 'evidence/nvn/main_nvn_ids.txt'], expected=3)
    if run('loader-build', [cc, '-O2', '-Wall', '-Wextra',
                            'src/runtime/nso/nso_loader.c', 'src/runtime/nso/nso_call.c',
                            '-ldl', '-o', native]):
        if module.exists():
            run('module-load-info', [native, module.resolve(), '--info'])
        else:
            results.append(dict(name='module-load-info', passed=False,
                                blocked='local unpacked game module is absent'))
    probe = out / 'probe-build'
    if run('probe-configure', [cmake, '-S', 'src/android/native', '-B', probe,
                               '-DCMAKE_BUILD_TYPE=Release']):
        if run('probe-build', [cmake, '--build', probe, '-j', '2']):
            run('probe-json-tests', [sys.executable, 'tests/device/test_probe_output.py',
                                     probe / 'probe_rg477m'])
    hashes = {}
    for file in (native, graphics, coverage, probe / 'probe_rg477m'):
        if file.is_file():
            hashes[str(file)] = hashlib.sha256(file.read_bytes()).hexdigest()
    summary = dict(schema_version=1, scope='host build and ordinary functional checks',
                   source_head=subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root,
                                              capture_output=True, text=True).stdout.strip(),
                   source_dirty=subprocess.run(['git', '--no-optional-locks', 'status', '--short'],
                                               cwd=root, capture_output=True, text=True).stdout,
                   checks=results, artifact_sha256=hashes,
                   passed=all(item['passed'] for item in results),
                   game_entry='NOT_RUN', android_build='NOT_RUN',
                   title_screen='NOT_RUN', playable='NOT_RUN', device='NOT_RUN')
    (out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
