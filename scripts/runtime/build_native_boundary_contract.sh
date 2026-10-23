#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN="${NB_BUILD_DIR:-${ROOT}/.local/native-host-callback/$(date -u +%Y%m%dT%H%M%SZ)}"
TOOLCHAIN="${NB_TOOLCHAIN:-${ROOT}/../ffrs-android/.local/minimal-ndk/toolchain}"
RUST_ROOT="${NB_RUST_ROOT:-${HOME}/.rustup/toolchains/stable-aarch64-apple-darwin}"
LINKER="${RUST_ROOT}/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
if [[ -e "${RUN}" ]]; then echo "refusing to overwrite ${RUN}" >&2; exit 2; fi
mkdir -p "${RUN}/logs" "${RUN}/source_freeze/src/runtime/nso" \
    "${RUN}/source_freeze/tests/native" "${RUN}/source_freeze/scripts/runtime"
for file in src/runtime/nso/native_boundary.c src/runtime/nso/native_boundary.h \
    src/runtime/nso/native_boundary.S tests/native/native_boundary_contract.c \
    scripts/runtime/build_native_boundary_contract.sh; do
    cp "${ROOT}/${file}" "${RUN}/source_freeze/${file}"
done
git -C "${ROOT}" rev-parse HEAD > "${RUN}/git-head.txt"
git -C "${ROOT}" status --short > "${RUN}/git-status.txt"
run() {
    local label="$1" code=0; shift
    printf '%q ' "$@" > "${RUN}/logs/${label}.command"
    printf '\n' >> "${RUN}/logs/${label}.command"
    "$@" > "${RUN}/logs/${label}.stdout" 2> "${RUN}/logs/${label}.stderr" || code=$?
    printf '%s\n' "${code}" > "${RUN}/logs/${label}.exitcode"
    if [[ "${label}" != android-disassembly && "${label}" != android-symbols &&
          "${label}" != baseline-component-source ]]; then
        cat "${RUN}/logs/${label}.stdout" "${RUN}/logs/${label}.stderr"
    fi
    return "${code}"
}
SOURCE="${RUN}/source_freeze"
run host-compile clang -std=c11 -Wall -Wextra -Werror -Wpedantic \
    "${SOURCE}/src/runtime/nso/native_boundary.c" "${SOURCE}/tests/native/native_boundary_contract.c" \
    -o "${RUN}/native_boundary.host"
run host-platform-check "${RUN}/native_boundary.host"
SYSROOT="${TOOLCHAIN}/sysroot"
RESOURCE="${TOOLCHAIN}/lib/clang/18"
API="${SYSROOT}/usr/lib/aarch64-linux-android/28"
FLAGS=(--target=aarch64-linux-android28 --sysroot="${SYSROOT}" -resource-dir "${RESOURCE}"
    -isystem "${SYSROOT}/usr/include/aarch64-linux-android" -std=c11 -O2 -g
    -fPIE -ffixed-x18 -fno-omit-frame-pointer -Wall -Wextra -Werror -Wpedantic -mbranch-protection=bti)
run compiler-version clang --version
run android-component clang "${FLAGS[@]}" -MD -MF "${RUN}/component.d" -c "${SOURCE}/src/runtime/nso/native_boundary.c" -o "${RUN}/native_boundary.o"
run android-assembly clang "${FLAGS[@]}" -c "${SOURCE}/src/runtime/nso/native_boundary.S" -o "${RUN}/native_boundary_asm.o"
run android-contract clang "${FLAGS[@]}" -fsanitize=shadow-call-stack -MD -MF "${RUN}/contract.d" \
    -c "${SOURCE}/tests/native/native_boundary_contract.c" -o "${RUN}/native_boundary_contract.o"
run cleanup-contract clang "${FLAGS[@]}" -DNB_CLEANUP_CONTRACT=1 -MD -MF "${RUN}/cleanup_contract.d" \
    -c "${SOURCE}/tests/native/native_boundary_contract.c" -o "${RUN}/cleanup_contract.o"
# Regression evidence uses the unchanged original component and exactly the
# same new cleanup test. No game or unrelated historical workflow is read.
run baseline-component-source git -C "${ROOT}" show \
    2fca9614b59332a3ef78a024fce2b379302492a4:src/runtime/nso/native_boundary.c
cp "${RUN}/logs/baseline-component-source.stdout" "${RUN}/baseline_native_boundary.c"
run baseline-component clang "${FLAGS[@]}" -I "${SOURCE}/src/runtime/nso" \
    -MD -MF "${RUN}/baseline_component.d" -c "${RUN}/baseline_native_boundary.c" \
    -o "${RUN}/baseline_native_boundary.o"
run android-link env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" \
    --no-undefined --build-id=sha1 --eh-frame-hdr -pie --dynamic-linker /system/bin/linker64 \
    -z max-page-size=16384 -z noexecstack -z relro -z now -o "${RUN}/native_boundary.android" \
    "${API}/crtbegin_dynamic.o" "${RUN}/native_boundary.o" "${RUN}/native_boundary_asm.o" \
    "${RUN}/native_boundary_contract.o" --start-group \
    "${RESOURCE}/lib/linux/libclang_rt.builtins-aarch64-android.a" -L "${API}" -lc --end-group \
    "${API}/crtend_android.o"
for variant in cleanup regression; do
    COMPONENT="${RUN}/native_boundary.o"
    if [[ "${variant}" == regression ]]; then COMPONENT="${RUN}/baseline_native_boundary.o"; fi
    run "${variant}-link" env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" \
        --no-undefined --build-id=sha1 --eh-frame-hdr -pie --dynamic-linker /system/bin/linker64 \
        -z max-page-size=16384 -z noexecstack -z relro -z now --wrap=mprotect --wrap=munmap \
        -o "${RUN}/native_boundary_${variant}.android" "${API}/crtbegin_dynamic.o" \
        "${COMPONENT}" "${RUN}/native_boundary_asm.o" "${RUN}/cleanup_contract.o" --start-group \
        "${RESOURCE}/lib/linux/libclang_rt.builtins-aarch64-android.a" -L "${API}" -lc --end-group \
        "${API}/crtend_android.o"
done
run android-elf "${TOOLCHAIN}/bin/llvm-readelf" --file-header --program-headers --dynamic-table \
    "${RUN}/native_boundary.android"
run android-disassembly /Library/Developer/CommandLineTools/usr/bin/llvm-objdump -d "${RUN}/native_boundary.android"
run android-symbols "${TOOLCHAIN}/bin/llvm-readelf" --symbols "${RUN}/native_boundary.android"
python3 - "${RUN}" "${TOOLCHAIN}" <<'PY'
import hashlib,json,pathlib,sys,shlex,zlib,struct
run=pathlib.Path(sys.argv[1]); tc=pathlib.Path(sys.argv[2]).resolve(); files=[]
for p in sorted((run/'source_freeze').rglob('*')):
    if p.is_file():files.append({'path':str(p.relative_to(run/'source_freeze')),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
artifact=run/'native_boundary.android'
known=json.loads((tc.parent/'validated-members.json').read_text());members={v['path']:v for v in known['members']}
inputs=set()
for dep in ['component.d','contract.d','cleanup_contract.d','baseline_component.d']:
    inputs.update(pathlib.Path(v).resolve() for v in shlex.split((run/dep).read_text().replace('\\\n',' ').split(':',1)[1]))
api=tc/'sysroot/usr/lib/aarch64-linux-android/28'
inputs.update([api/'crtbegin_dynamic.o',api/'crtend_android.o',api/'libc.so',tc/'lib/clang/18/lib/linux/libclang_rt.builtins-aarch64-android.a'])
input_records=[]
for p in sorted(inputs):
    data=p.read_bytes();record={'path':str(p),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    if p.is_relative_to(tc):
        name=str(p.relative_to(tc));crc=f'{zlib.crc32(data)&0xffffffff:08x}'
        assert name in members and members[name]['crc32']==crc and members[name]['bytes']==len(data),name
        record['crc32_rechecked']=crc
    input_records.append(record)
def artifact_record(path,expected_exit):
    data=path.read_bytes();h=struct.unpack_from('<16sHHIQQQIHHHHHH',data)
    assert data[:6]==b'\x7fELF\x02\x01' and h[1]==3 and h[2]==183
    ph=[struct.unpack_from('<IIQQQQQQ',data,h[5]+i*h[9])for i in range(h[10])]
    assert all(p[7]>=16384 and p[2]%16384==p[3]%16384 and not(p[1]&1 and p[1]&2)for p in ph if p[0]==1)
    assert any(p[0]==0x6474e552 for p in ph) and any(p[0]==0x6474e551 and not p[1]&1 for p in ph)
    return {'path':str(path),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'expected_contract_exit':expected_exit}
symbols=(run/'logs/android-symbols.stdout').read_text()
assert '__wrap_mprotect' not in symbols and '__wrap_munmap' not in symbols
assert 'fail_protect' not in symbols and 'fail_unmap' not in symbols
assert 'nb_call_stack13' in symbols and 'nb_import_stack13_gateway' in symbols
assert 'nb_call_from_host' in symbols
logs=[{'command':p.name,'command_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'exit_code':int(p.with_suffix('.exitcode').read_text())}for p in sorted((run/'logs').glob('*.command'))]
manifest={'android_execution':'NOT_RUN','host_native_boundary_execution':'UNSUPPORTED','git_head_at_build':(run/'git-head.txt').read_text().strip(),'sources':files,'commands':logs,'inputs':input_records,'artifact':artifact_record(artifact,0),'cleanup_artifact':artifact_record(run/'native_boundary_cleanup.android',0),'regression_artifact':artifact_record(run/'native_boundary_regression.android',1),'normal_test_hooks':False,'regression_component_commit':'2fca9614b59332a3ef78a024fce2b379302492a4','regression_component_sha256':hashlib.sha256((run/'baseline_native_boundary.c').read_bytes()).hexdigest(),'toolchain_provenance':'existing Google partial NDK members CRC validated; whole archive SHA1 not verified'}
manifest['abi_modes']={'v1':'register-only; gateway saves active guest x18 in an enlarged private frame','v1_host_import_frame_bytes':48,'v2':'declared GP21 scalar/pointer u64 only','v2_stack_payload_bytes':104,'v2_stack_words':13,'v2_outgoing_area_bytes':112,'v2_guest_entry_frame_bytes':368,'v2_host_import_frame_bytes':160,'host_callback':'owned HOST phase only; register-only, actual active guest x18 seed','host_callback_frame_bytes':272,'host_callback_max_depth':4,'host_callback_depth_status':10,'scope_bytes':64,'scope_active_guest_x18_offset':48,'scope_callback_depth_offset':56,'variadic_aggregate_unknown_layout':'UNSUPPORTED','musl_implementation':False}
manifest['source_bundle_sha256']=hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()
(run/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for key in ['artifact','cleanup_artifact','regression_artifact']:print(json.dumps({key:manifest[key]}))
PY
printf 'Android execution NOT_RUN; scoped evidence %s\n' "${RUN}"
