#!/usr/bin/env python3
"""Reproduce ordinary host checks, retaining actual process return codes.

This suite never launches a game entry, emulator, or device. An explicit
--game-leaf option executes one byte-identified routine from the local module.
Success proves only the named host checks, not Android or playable gates.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('.local/host-verification'))
    parser.add_argument('--module', type=Path, default=Path('.local/uncompressed/main.bin'))
    parser.add_argument('--runtime-contracts', action='store_true',
                        help='run ordinary synchronization, reservation, isolated TLS and exit contracts on macOS ARM64')
    parser.add_argument('--game-leaf', action='store_true',
                        help='compare one original local game routine with its mathematical oracle')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    module = args.module if args.module.is_absolute() else root / args.module
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = []
    extra_artifacts = []
    module_before = hashlib.sha256(module.read_bytes()).hexdigest() if module.is_file() else None

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
    if args.runtime_contracts:
        if platform.system() != 'Darwin' or platform.machine() != 'arm64':
            results.append(dict(name='runtime-contracts', passed=False,
                                blocked='these independent VM/TLS contracts require macOS ARM64'))
        else:
            contracts = [('mutex', 'mutex', 'mutex_contract'),
                         ('address-region', 'address_region', 'address_region_contract'),
                         ('guest-tls', 'guest_tls', 'guest_tls_threads'),
                         ('exit-registry', 'exit_registry', 'exit_registry_contract')]
            for name, implementation, harness in contracts:
                binary = out / ('test-' + name)
                extra_artifacts.append(binary)
                sources = [f'src/runtime/hle/{implementation}.c', f'tests/native/{harness}.c']
                flags = [cc, '-O2', '-Wall', '-Wextra', '-Werror', '-pthread']
                if name == 'mutex':
                    mutex_obj = out / 'mutex.o'
                    clock_obj = out / 'host-monotonic-cond.o'
                    extra_artifacts.extend([mutex_obj, clock_obj])
                    built = run(name + '-impl', flags + ['-include', 'tests/native/host_monotonic_cond.h',
                                                         '-c', sources[0], '-o', mutex_obj])
                    built = built and run(name + '-clock', [cc, '-O2', '-Wall', '-Wextra', '-Werror',
                                                            '-c', 'tests/native/host_monotonic_cond.c', '-o', clock_obj])
                    built = built and run(name + '-build', flags + [str(mutex_obj), str(clock_obj), sources[1], '-o', binary])
                elif name == 'exit-registry':
                    built = run(name + '-build', flags + sources + ['tests/native/host_exit_registry_stub.c', '-o', binary])
                else:
                    built = run(name + '-build', flags + sources + ['-o', binary])
                if built:
                    run(name + '-contract', [binary])
                    if name == 'guest-tls':
                        run(name + '-host-control', [binary, '--host-only-control'])
                    if name == 'exit-registry':
                        run('normal-exit', [binary, '--normal-exit'], expected=23)
                        run('immediate-exit', [binary, '--immediate-exit'], expected=24)
                        normal = (out / 'normal-exit.stdout').read_text()
                        immediate = (out / 'immediate-exit.stdout').read_text()
                        message = 'guest normal-exit callback executed'
                        results.append(dict(name='exit-callback-policy',
                                            passed=normal.count(message) == 1 and message not in immediate))
    if args.game_leaf:
        expected_sha = '6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5'
        if platform.machine() != 'arm64' or module_before != expected_sha:
            results.append(dict(name='game-leaf', passed=False,
                                blocked='native ARM64 and the byte-identified local module are required'))
        else:
            leaf = out / 'game-leaf'
            extra_artifacts.append(leaf)
            if run('game-leaf-build', [cc, '-O2', '-Wall', '-Wextra', '-Werror',
                                       'src/runtime/nso/nso_loader.c',
                                       'tests/native/game_leaf_sample.c', '-o', leaf]):
                run('game-leaf-comparison', [leaf, module.resolve()])
    event_binary = out / 'test-event-finalize'
    extra_artifacts.append(event_binary)
    event_flags = [cc, '-std=c11', '-Wall', '-Wextra', '-pthread', '-fsanitize=address,undefined']
    if run('event-finalize-asan-build', event_flags + [
            'src/runtime/hle/nso_host.c', 'tests/native/event_finalize_contract.c',
            'tests/native/event_finalize_stubs.c', '-o', event_binary]):
        run('event-finalize-asan', [event_binary])
    event_tsan = out / 'test-event-finalize-tsan'
    extra_artifacts.append(event_tsan)
    tsan_flags = [cc, '-std=c11', '-Wall', '-Wextra', '-pthread', '-fsanitize=thread']
    if run('event-finalize-tsan-build', tsan_flags + [
            'src/runtime/hle/nso_host.c', 'tests/native/event_finalize_contract.c',
            'tests/native/event_finalize_stubs.c', '-o', event_tsan]):
        run('event-finalize-tsan', [event_tsan])
    retire_binary = out / 'test-event-retire'
    extra_artifacts.append(retire_binary)
    if run('event-retire-asan-build', event_flags + [
            'src/runtime/hle/nso_host.c', 'tests/native/event_retire_contract.c',
            'tests/native/event_finalize_stubs.c', '-o', retire_binary]):
        run('event-retire-asan', [retire_binary])
    retire_tsan = out / 'test-event-retire-tsan'
    extra_artifacts.append(retire_tsan)
    if run('event-retire-tsan-build', tsan_flags + [
            'src/runtime/hle/nso_host.c', 'tests/native/event_retire_contract.c',
            'tests/native/event_finalize_stubs.c', '-o', retire_tsan]):
        run('event-retire-tsan', [retire_tsan])
    save_binary = out / 'test-save-commit'
    extra_artifacts.append(save_binary)
    if run('save-commit-build', [cc, '-std=c11', '-Wall', '-Wextra', '-pthread', '-fsanitize=address,undefined',
                                  'src/runtime/hle/nso_host.c', 'tests/native/save_commit_host_contract.c',
                                  'tests/native/event_finalize_stubs.c', '-o', save_binary]):
        run('save-commit-contract', [save_binary])
    warm_flush = out / 'test-warm-flush'
    extra_artifacts.append(warm_flush)
    warm_cxx = shutil.which('clang++') or shutil.which('c++')
    if not warm_cxx:
        results.append(dict(name='warm-flush-build', passed=False,
                            blocked='a C++ compiler is required for the warm flush contract'))
    elif run('warm-flush-build', [warm_cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                                  '-Isrc/graphics/nvn_vulkan/engine',
                                  'tests/graphics/test_warm_flush_contract.cpp', '-o', str(warm_flush)]):
        run('warm-flush-contract', [warm_flush, str(root)])
    run('portable-package', [sys.executable, '-m', 'unittest',
                              'tests.installer.test_package_honor_portable'])
    spirv_as = shutil.which('spirv-as')
    spirv_val = shutil.which('spirv-val')
    if spirv_as and spirv_val:
        typed_dir = out / 'typed-storage'
        typed_dir.mkdir(parents=True, exist_ok=True)
        typed_spv = []
        for fixture in ('typed_storage_uint', 'typed_storage_shared_image', 'typed_storage_formatless_read',
                        'typed_storage_game_declared'):
            spv = typed_dir / (fixture + '.spv')
            if run('typed-storage-assemble-' + fixture, [spirv_as, '--target-env', 'vulkan1.1',
                                              'tests/device/' + fixture + '.spvasm', '-o', spv]):
                typed_spv.append(spv)
        typed_cxx = shutil.which('clang++') or cc
        typed_binary = out / 'test-typed-storage'
        extra_artifacts.append(typed_binary)
        if len(typed_spv) == 4 and run('typed-storage-build', [typed_cxx, '-std=c++17', '-O1', '-g',
                                                    '-fsanitize=address,undefined', '-Wall', '-Wextra',
                                                    'tests/device/test_typed_storage.cpp', '-o', typed_binary]):
            run('typed-storage-contract', [typed_binary, typed_spv[0], typed_spv[1], typed_spv[2],
                                       typed_dir / 'typed_storage_out.spv', typed_spv[3]])
            run('typed-storage-val', [spirv_val, '--target-env', 'vulkan1.1', typed_dir / 'typed_storage_out.spv'])
        # The real module that crashed the 1.0.9 battle settle scene on the Honor
        # tablet (c11e2f23ff063e85) declares its own typed storage images. The game
        # data is not in the repository, so this check runs only when the module
        # has been extracted locally; otherwise it is skipped and says so.
        real_module = Path(os.environ.get('FFRS_REAL_MODULE', '')) if os.environ.get('FFRS_REAL_MODULE') else None
        if real_module and real_module.is_file():
            real_binary = out / 'test-typed-storage-real'
            extra_artifacts.append(real_binary)
            if run('typed-storage-real-build', [typed_cxx, '-std=c++17', '-O1', '-g',
                                                '-fsanitize=address,undefined', '-Wall', '-Wextra',
                                                'tests/device/test_typed_storage_real_module.cpp', '-o', real_binary]):
                run('typed-storage-real-contract', [real_binary, str(real_module)])
        else:
            print('typed-storage-real-contract: SKIPPED, set FFRS_REAL_MODULE to the extracted c11e2f23ff063e85.spv',
                  flush=True)
    else:
        print('typed-storage-contract: SKIPPED, spirv-as and spirv-val are not installed', flush=True)
    run('engine-log-rotation', [sys.executable, '-m', 'unittest', 'tests.installer.test_engine_log_rotation'])
    run('present-outcome', [sys.executable, '-m', 'unittest', 'tests.installer.test_present_outcome'])
    probe = out / 'probe-build'

    if run('probe-configure', [cmake, '-S', 'src/android/native', '-B', probe,
                               '-DCMAKE_BUILD_TYPE=Release']):
        if run('probe-build', [cmake, '--build', probe, '-j', '2']):
            run('probe-json-tests', [sys.executable, 'tests/device/test_probe_output.py',
                                     probe / 'probe_rg477m'])
    hashes = {}
    for file in (native, graphics, coverage, probe / 'probe_rg477m', *extra_artifacts):
        if file.is_file():
            hashes[str(file)] = hashlib.sha256(file.read_bytes()).hexdigest()
    module_after = hashlib.sha256(module.read_bytes()).hexdigest() if module.is_file() else None
    if module_after != module_before:
        results.append(dict(name='input-unchanged', passed=False, blocked='local module changed during checks'))
    summary = dict(schema_version=1, scope='host build and ordinary functional checks; optional isolated game leaf',
                   source_head=subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root,
                                              capture_output=True, text=True).stdout.strip(),
                   source_dirty=subprocess.run(['git', '--no-optional-locks', 'status', '--short'],
                                               cwd=root, capture_output=True, text=True).stdout,
                   checks=results, artifact_sha256=hashes,
                   input_sha256_before=module_before, input_sha256_after=module_after,
                   original_game_leaf_requested=args.game_leaf,
                   passed=all(item['passed'] for item in results),
                   game_entry='NOT_RUN', android_build='NOT_RUN',
                   title_screen='NOT_RUN', playable='NOT_RUN', device='NOT_RUN')
    (out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
