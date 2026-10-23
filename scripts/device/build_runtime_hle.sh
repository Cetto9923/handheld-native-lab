#!/usr/bin/env bash
# Standalone Android HLE CLI build and static inspection. NEVER executes it.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android_env.sh"
FFRS_LOGS="$(mktemp -d "${FFRS_ROOT}/.local/logs/android-hle-build.XXXXXX")"
FFRS_BUILD="$(mktemp -d "${FFRS_ROOT}/.local/build/android-hle.XXXXXX")"
FFRS_ARTIFACTS="${FFRS_ROOT}/.local/artifacts/android-runtime-hle"
mkdir -p "${FFRS_ARTIFACTS}"
RUNTIME_DIR="${FFRS_RUNTIME_DIR:-${FFRS_ROOT}/src/runtime/nso}"
HLE_DIR="${FFRS_HLE_DIR:-${RUNTIME_DIR}/../hle}"
finish_build() {
    local result=$?
    trap - EXIT
    python3 "${DEVICE_SCRIPT_DIR}/record_build_evidence.py" --kind runtime-hle --source-root "${FFRS_ROOT}" \
        --logs "${FFRS_LOGS}" --artifacts "${FFRS_ARTIFACTS}" --overall-exit "${result}" || result=6
    printf 'HLE CLI evidence: %s/build-evidence.json\nStep logs: %s\n' "${FFRS_ARTIFACTS}" "${FFRS_LOGS}"
    exit "${result}"
}
trap finish_build EXIT
printf 'NOT_RUN\n' > "${FFRS_LOGS}/source-guard.exitcode"
snapshot_sources() {
    python3 "${DEVICE_SCRIPT_DIR}/snapshot_build_inputs.py" --source-root "${FFRS_ROOT}" \
        --runtime "${RUNTIME_DIR}" --additional-dir "${HLE_DIR}"
}
snapshot_sources > "${FFRS_LOGS}/build-input-before.json"
for source in "${RUNTIME_DIR}/nso_call.c" "${RUNTIME_DIR}/nso_loader.c" \
    "${HLE_DIR}/nso_host.c" "${HLE_DIR}/address_region.c" "${HLE_DIR}/mutex.c" "${HLE_DIR}/exit_registry.c"; do
    if [[ ! -f "${source}" ]]; then
        echo "BLOCKED_SOURCE: missing fixed runtime dependency ${source}" >&2
        exit 2
    fi
done
if [[ "${FFRS_USE_MINIMAL_TOOLCHAIN:-0}" == 1 ]]; then
    SYSROOT="${FFRS_LLVM}/sysroot"
    RESOURCE="${FFRS_LLVM}/lib/clang/18"
    API_LIBS="${SYSROOT}/usr/lib/aarch64-linux-android/28"
    RUST_ROOT="${FFRS_RUST_ROOT:-${HOME}/.rustup/toolchains/stable-aarch64-apple-darwin}"
    LINKER="${RUST_ROOT}/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
    COMMON=(--target=aarch64-linux-android28 --sysroot="${SYSROOT}" -resource-dir "${RESOURCE}"
        -isystem "${SYSROOT}/usr/include/aarch64-linux-android" -fPIC -O2 -g -fno-omit-frame-pointer
        -I "${RUNTIME_DIR}" -I "${HLE_DIR}" -std=c11 -Wall -Wextra -Werror -Wformat=2 -DNSO_WITH_HOST_HLE=1)
    OBJECTS=()
    for source in "${RUNTIME_DIR}/nso_call.c" "${RUNTIME_DIR}/nso_loader.c" \
        "${HLE_DIR}/nso_host.c" "${HLE_DIR}/address_region.c" "${HLE_DIR}/mutex.c" "${HLE_DIR}/exit_registry.c"; do
        name="$(basename "${source}" .c)"
        ffrs_run "runtime-hle-compile-${name}" /usr/bin/clang "${COMMON[@]}" -c "${source}" -o "${FFRS_BUILD}/${name}.o"
        OBJECTS+=("${FFRS_BUILD}/${name}.o")
    done
    ffrs_run runtime-hle-link env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" \
        -pie --dynamic-linker /system/bin/linker64 --no-undefined -z max-page-size=16384 --build-id=sha1 \
        -z relro -z now -z noexecstack -o "${FFRS_BUILD}/nso_runtime_hle" "${API_LIBS}/crtbegin_dynamic.o" \
        "${OBJECTS[@]}" "${RESOURCE}/lib/linux/libclang_rt.builtins-aarch64-android.a" \
        -L "${API_LIBS}" -lc -lm -ldl "${API_LIBS}/crtend_android.o"
else
    ffrs_run runtime-hle-configure "${FFRS_CMAKE}" -S "${FFRS_ROOT}/src/android/native" -B "${FFRS_BUILD}" \
        -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE="${FFRS_NDK}/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFFRS_BUILD_RUNTIME_HLE=ON \
        -DFFRS_RUNTIME_DIR="${RUNTIME_DIR}" -DFFRS_HLE_DIR="${HLE_DIR}" -DFFRS_READELF="${FFRS_LLVM}/bin/llvm-readelf"
    ffrs_run runtime-hle-link "${FFRS_CMAKE}" --build "${FFRS_BUILD}" --target nso_runtime_hle -j "${FFRS_BUILD_JOBS:-4}"
fi
snapshot_sources > "${FFRS_LOGS}/build-input-after-compile.json"
if ! cmp -s "${FFRS_LOGS}/build-input-before.json" "${FFRS_LOGS}/build-input-after-compile.json"; then
    printf '5\n' > "${FFRS_LOGS}/source-guard.exitcode"
    echo 'SOURCE_CHANGED: HLE CLI input changed during compilation.' >&2
    exit 5
fi
ffrs_run runtime-hle-elf python3 "${FFRS_ROOT}/tests/device/verify_runtime_hle_elf.py" \
    --elf "${FFRS_BUILD}/nso_runtime_hle" --readelf "${FFRS_LLVM}/bin/llvm-readelf" \
    --output "${FFRS_LOGS}/runtime-hle-elf.json"
snapshot_sources > "${FFRS_LOGS}/build-input-after.json"
if ! cmp -s "${FFRS_LOGS}/build-input-before.json" "${FFRS_LOGS}/build-input-after.json"; then
    printf '5\n' > "${FFRS_LOGS}/source-guard.exitcode"
    echo 'SOURCE_CHANGED: HLE CLI input changed before final verification.' >&2
    exit 5
fi
ffrs_run runtime-hle-stage cp "${FFRS_BUILD}/nso_runtime_hle" "${FFRS_ARTIFACTS}/nso_runtime_hle"
printf '0\n' > "${FFRS_LOGS}/source-guard.exitcode"
printf 'Built %s/nso_runtime_hle; Android execution NOT_RUN; guest TLS SDK binding NOT_IMPLEMENTED.\n' "${FFRS_ARTIFACTS}"
