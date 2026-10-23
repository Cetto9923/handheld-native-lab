#!/usr/bin/env bash
# Shared discovery. A directory is never sufficient proof of an installed NDK.
set -euo pipefail
DEVICE_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFRS_ROOT="$(cd "${DEVICE_SCRIPT_DIR}/../.." && pwd)"
FFRS_ROOT="${FFRS_SOURCE_ROOT:-${FFRS_ROOT}}"
FFRS_SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-${HOME}/Library/Android/sdk}}"
FFRS_NDK="${ANDROID_NDK_ROOT:-}"
if [[ "${FFRS_USE_MINIMAL_TOOLCHAIN:-0}" == 1 ]]; then
    FFRS_LLVM="${FFRS_ROOT}/.local/minimal-ndk/toolchain"
    if [[ ! -f "${FFRS_ROOT}/.local/minimal-ndk/validated-members.json" ]]; then
        echo "BLOCKED_TOOLCHAIN: partial official sysroot has no validation manifest." >&2
        exit 2
    fi
elif [[ -z "${FFRS_NDK}" ]]; then
    for candidate in "${FFRS_ROOT}"/.local/sdk/ndk/* "${FFRS_SDK}"/ndk/*; do
        if [[ -f "${candidate}/source.properties" && -f "${candidate}/build/cmake/android.toolchain.cmake" ]]; then
            FFRS_NDK="${candidate}"
            break
        fi
    done
fi
if [[ "${FFRS_USE_MINIMAL_TOOLCHAIN:-0}" != 1 ]]; then
if [[ ! -f "${FFRS_NDK}/source.properties" || ! -f "${FFRS_NDK}/build/cmake/android.toolchain.cmake" ]]; then
    echo "BLOCKED_TOOLCHAIN: no complete Android NDK; set ANDROID_NDK_ROOT." >&2
    exit 2
fi
FFRS_LLVM=""
for candidate in "${FFRS_NDK}"/toolchains/llvm/prebuilt/*; do
    if [[ -x "${candidate}/bin/clang" && -x "${candidate}/bin/llvm-readelf" ]]; then
        FFRS_LLVM="${candidate}"
        break
    fi
done
if [[ -z "${FFRS_LLVM}" ]]; then
    echo "BLOCKED_TOOLCHAIN: installed NDK has no runnable LLVM toolchain." >&2
    exit 2
fi
fi
FFRS_CMAKE="${CMAKE_BIN:-$(command -v cmake || true)}"
if [[ ! -x "${FFRS_CMAKE}" ]]; then
    echo "BLOCKED_TOOLCHAIN: CMake is unavailable." >&2
    exit 2
fi
FFRS_BUILD="${FFRS_ANDROID_BUILD_DIR:-${FFRS_ROOT}/.local/build/android-arm64}"
if [[ "${FFRS_USE_MINIMAL_TOOLCHAIN:-0}" == 1 ]]; then
    FFRS_BUILD="${FFRS_ANDROID_BUILD_DIR:-${FFRS_ROOT}/.local/build/android-arm64-minimal}"
fi
FFRS_ARTIFACTS="${FFRS_ROOT}/.local/artifacts/android"
FFRS_LOGS="${FFRS_ANDROID_LOG_DIR:-${FFRS_ROOT}/.local/logs/android}"
mkdir -p "${FFRS_BUILD}" "${FFRS_ARTIFACTS}" "${FFRS_LOGS}"
# Each step preserves the executed command, full output, and actual command exit.
ffrs_run() {
    local label="$1"
    shift
    printf '%q ' "$@" > "${FFRS_LOGS}/${label}.command"
    printf '\n' >> "${FFRS_LOGS}/${label}.command"
    local result=0
    "$@" > "${FFRS_LOGS}/${label}.log" 2>&1 || result=$?
    printf '%s\n' "${result}" > "${FFRS_LOGS}/${label}.exitcode"
    cat "${FFRS_LOGS}/${label}.log"
    return "${result}"
}
