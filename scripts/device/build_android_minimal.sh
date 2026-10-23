#!/usr/bin/env bash
# True Android ELF build with Apple Clang, Rust LLD, and CRC-verified official NDK members.
# This explicitly is NOT an installed/full-checksum-validated Android NDK.
set -euo pipefail
export FFRS_USE_MINIMAL_TOOLCHAIN=1
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android_env.sh"
SYSROOT="${FFRS_LLVM}/sysroot"
RESOURCE="${FFRS_LLVM}/lib/clang/18"
TARGET_LIBS="${SYSROOT}/usr/lib/aarch64-linux-android"
API_LIBS="${TARGET_LIBS}/28"
RUST_ROOT="${FFRS_RUST_ROOT:-${HOME}/.rustup/toolchains/stable-aarch64-apple-darwin}"
LINKER="${RUST_ROOT}/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
RUNTIME_DIR="${FFRS_RUNTIME_DIR:-${FFRS_ROOT}/src/runtime/nso}"
NVN_DIR="${FFRS_NVN_DIR:-${FFRS_ROOT}/src/graphics/nvn}"
if [[ ! -x "${LINKER}" || ! -f "${RESOURCE}/lib/linux/aarch64/libunwind.a" ]]; then
    echo 'BLOCKED_TOOLCHAIN: Rust LLD or validated NDK aarch64 libunwind is absent.' >&2
    exit 2
fi
mkdir -p "${FFRS_BUILD}/objects"
COMMON=(--target=aarch64-linux-android28 --sysroot="${SYSROOT}" -resource-dir "${RESOURCE}"
    -isystem "${SYSROOT}/usr/include/aarch64-linux-android" -fPIC -O2 -g -fno-omit-frame-pointer
    -I "${FFRS_ROOT}/src/android/native" -I "${RUNTIME_DIR}")
CXX=(-nostdinc++ -isystem "${SYSROOT}/usr/include/c++/v1" -std=c++20)
OBJECTS=()
for source in probe_core probe_types probe_platform json_writer probe_vulkan probe_android; do
    ffrs_run "minimal-compile-${source}" clang++ "${COMMON[@]}" "${CXX[@]}" -c \
        "${FFRS_ROOT}/src/android/native/${source}.cpp" -o "${FFRS_BUILD}/objects/${source}.o"
    OBJECTS+=("${FFRS_BUILD}/objects/${source}.o")
done
ffrs_run minimal-compile-main clang++ "${COMMON[@]}" "${CXX[@]}" -c \
    "${FFRS_ROOT}/src/android/native/main.cpp" -o "${FFRS_BUILD}/objects/main.o"
LINK=(env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" -z max-page-size=16384 --build-id=sha1)
LIBRARIES=(--start-group "${TARGET_LIBS}/libc++_static.a" "${TARGET_LIBS}/libc++abi.a"
    "${RESOURCE}/lib/linux/aarch64/libunwind.a" "${RESOURCE}/lib/linux/libclang_rt.builtins-aarch64-android.a"
    --end-group -L "${API_LIBS}" -lc -lm -ldl -llog -landroid -lvulkan)
ffrs_run minimal-link-probe "${LINK[@]}" -pie --dynamic-linker /system/bin/linker64 \
    -o "${FFRS_BUILD}/probe_rg477m" "${API_LIBS}/crtbegin_dynamic.o" "${OBJECTS[@]}" \
    "${FFRS_BUILD}/objects/main.o" "${LIBRARIES[@]}" "${API_LIBS}/crtend_android.o"
cp "${FFRS_BUILD}/probe_rg477m" "${FFRS_ARTIFACTS}/probe_rg477m"
ffrs_run probe-elf "${FFRS_LLVM}/bin/llvm-readelf" --file-header --program-headers "${FFRS_ARTIFACTS}/probe_rg477m"
if [[ "${1:-}" == '--all' ]]; then
    ffrs_run minimal-compile-loader clang "${COMMON[@]}" -std=c11 -c \
        "${RUNTIME_DIR}/nso_loader.c" -o "${FFRS_BUILD}/objects/nso_loader.o"
    DIAGNOSTIC_FLAGS=()
    GRAPHICS_OBJECTS=()
    if [[ -f "${NVN_DIR}/src/nvn_dispatch.c" ]]; then
        ffrs_run minimal-compile-nvn clang "${COMMON[@]}" -std=c11 -I "${NVN_DIR}/include" -c \
            "${NVN_DIR}/src/nvn_dispatch.c" -o "${FFRS_BUILD}/objects/nvn_dispatch.o"
        DIAGNOSTIC_FLAGS=(-I "${NVN_DIR}/include" -DFFRS_HAS_NVN_DIAGNOSTICS=1)
        GRAPHICS_OBJECTS=("${FFRS_BUILD}/objects/nvn_dispatch.o")
    fi
    for source in nso_diagnostic diagnostic_main diagnostic_jni display_jni leaf_validation leaf_jni; do
        ffrs_run "minimal-compile-${source}" clang++ "${COMMON[@]}" "${CXX[@]}" "${DIAGNOSTIC_FLAGS[@]}" -c \
            "${FFRS_ROOT}/src/android/native/${source}.cpp" -o "${FFRS_BUILD}/objects/${source}.o"
    done
    NSO_OBJECTS=("${FFRS_BUILD}/objects/nso_loader.o" "${FFRS_BUILD}/objects/nso_diagnostic.o" "${GRAPHICS_OBJECTS[@]}")
    ffrs_run minimal-link-diagnostic "${LINK[@]}" -pie --dynamic-linker /system/bin/linker64 \
        -o "${FFRS_BUILD}/nso_diagnostic" "${API_LIBS}/crtbegin_dynamic.o" "${OBJECTS[@]}" \
        "${NSO_OBJECTS[@]}" "${FFRS_BUILD}/objects/diagnostic_main.o" "${LIBRARIES[@]}" "${API_LIBS}/crtend_android.o"
    ffrs_run minimal-link-jni "${LINK[@]}" -shared --no-undefined -soname libffrs_diagnostic.so \
        -o "${FFRS_BUILD}/libffrs_diagnostic.so" "${API_LIBS}/crtbegin_so.o" "${OBJECTS[@]}" \
        "${NSO_OBJECTS[@]}" "${FFRS_BUILD}/objects/diagnostic_jni.o" "${FFRS_BUILD}/objects/display_jni.o" "${FFRS_BUILD}/objects/leaf_validation.o" \
        "${FFRS_BUILD}/objects/leaf_jni.o" "${LIBRARIES[@]}" "${API_LIBS}/crtend_so.o"
    cp "${FFRS_BUILD}/nso_diagnostic" "${FFRS_ARTIFACTS}/nso_diagnostic"
    cp "${FFRS_BUILD}/libffrs_diagnostic.so" "${FFRS_ARTIFACTS}/libffrs_diagnostic.so"
fi
printf 'Actual Android arm64 ELF artifacts: %s\nPartial sysroot; whole NDK archive SHA1 remains pending.\n' "${FFRS_ARTIFACTS}"
