#!/usr/bin/env bash
# Build actual Android arm64 ELF artifacts; no device is required for this stage.
set -euo pipefail
if [[ "${FFRS_USE_MINIMAL_TOOLCHAIN:-0}" == 1 ]]; then
    exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_android_minimal.sh" "$@"
fi
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android_env.sh"
ffrs_run configure "${FFRS_CMAKE}" -S "${FFRS_ROOT}/src/android/native" -B "${FFRS_BUILD}" \
    -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE="${FFRS_NDK}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ffrs_run build-probe "${FFRS_CMAKE}" --build "${FFRS_BUILD}" --target probe_rg477m -j "${FFRS_BUILD_JOBS:-4}"
cp "${FFRS_BUILD}/probe_rg477m" "${FFRS_ARTIFACTS}/probe_rg477m"
ffrs_run probe-elf "${FFRS_LLVM}/bin/llvm-readelf" --file-header --program-headers "${FFRS_ARTIFACTS}/probe_rg477m"
if [[ "${1:-}" == "--all" ]]; then
    ffrs_run build-diagnostics "${FFRS_CMAKE}" --build "${FFRS_BUILD}" --target nso_diagnostic ffrs_diagnostic -j "${FFRS_BUILD_JOBS:-4}"
    cp "${FFRS_BUILD}/nso_diagnostic" "${FFRS_ARTIFACTS}/nso_diagnostic"
    cp "${FFRS_BUILD}/libffrs_diagnostic.so" "${FFRS_ARTIFACTS}/libffrs_diagnostic.so"
fi
printf 'Android ELF artifacts: %s\n' "${FFRS_ARTIFACTS}"
