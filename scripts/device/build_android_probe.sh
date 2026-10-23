#!/usr/bin/env bash
#
# scripts/device/build_android_probe.sh
# One-click cross-compilation script for RG477M hardware probe tool (probe_rg477m).
#
# Target:
#   - SoC: MediaTek Dimensity 8300 (4x Cortex-A715 + 4x Cortex-A510)
#   - GPU: Mali-G615 MC6 (Vulkan 1.3)
#   - OS: Android 14 (API 34)
#   - ABI: arm64-v8a
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "=== RG477M Android Cross-Compilation: probe_rg477m ==="
echo "Worktree directory: ${WORKTREE_DIR}"

# 1. Locate CMake
if [[ -x "/opt/homebrew/bin/cmake" ]]; then
    CMAKE_BIN="/opt/homebrew/bin/cmake"
elif command -v cmake &>/dev/null; then
    CMAKE_BIN="$(command -v cmake)"
else
    echo "ERROR: CMake not found in /opt/homebrew/bin/cmake or PATH." >&2
    exit 1
fi
echo "Using CMake: ${CMAKE_BIN} ($("${CMAKE_BIN}" --version | head -n 1))"

# 2. Locate Android SDK & NDK
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/Users/yuyan9923/Library/Android/sdk}}"
if [[ ! -d "${ANDROID_SDK_ROOT}" ]]; then
    echo "ERROR: Android SDK directory not found at ${ANDROID_SDK_ROOT}" >&2
    exit 1
fi

# Locate NDK
if [[ -n "${ANDROID_NDK_ROOT:-}" && -d "${ANDROID_NDK_ROOT}" ]]; then
    NDK_DIR="${ANDROID_NDK_ROOT}"
elif [[ -d "${ANDROID_SDK_ROOT}/ndk/26.1.10909125" ]]; then
    NDK_DIR="${ANDROID_SDK_ROOT}/ndk/26.1.10909125"
else
    # Pick latest NDK installed
    NDK_DIR="$(find "${ANDROID_SDK_ROOT}/ndk" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -n 1 || true)"
fi

if [[ -z "${NDK_DIR}" || ! -d "${NDK_DIR}" ]]; then
    echo "ERROR: Android NDK not found in ${ANDROID_SDK_ROOT}/ndk/." >&2
    echo "Please wait for NDK installation or set ANDROID_NDK_ROOT." >&2
    exit 2
fi

TOOLCHAIN_FILE="${NDK_DIR}/build/cmake/android.toolchain.cmake"
if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
    echo "ERROR: CMake toolchain file not found at ${TOOLCHAIN_FILE}" >&2
    exit 2
fi
echo "Using Android NDK: ${NDK_DIR}"
echo "Using Toolchain:    ${TOOLCHAIN_FILE}"

# 3. Configure Build
BUILD_DIR="${WORKTREE_DIR}/build-android"
SOURCE_DIR="${WORKTREE_DIR}/src/android/native"
TARGET_ABI="arm64-v8a"
TARGET_API="34"
BUILD_TYPE="Release"

echo "Configuring CMake for Android ${TARGET_ABI} (API ${TARGET_API}, ${BUILD_TYPE})..."
"${CMAKE_BIN}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DANDROID_ABI="${TARGET_ABI}" \
    -DANDROID_PLATFORM="android-${TARGET_API}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "${SOURCE_DIR}"

# 4. Build Executable
NUM_CORES="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
echo "Building probe_rg477m with ${NUM_CORES} parallel jobs..."
"${CMAKE_BIN}" --build "${BUILD_DIR}" -j"${NUM_CORES}"

PROBE_BIN="${BUILD_DIR}/probe_rg477m"
if [[ ! -f "${PROBE_BIN}" ]]; then
    echo "ERROR: Build finished but ${PROBE_BIN} was not generated." >&2
    exit 3
fi

# 5. Verify Binary
echo "Build succeeded. Inspecting output artifact:"
ls -lh "${PROBE_BIN}"
file "${PROBE_BIN}"

# Stage binary into .local/bin
STAGE_DIR="${WORKTREE_DIR}/.local/bin"
mkdir -p "${STAGE_DIR}"
cp "${PROBE_BIN}" "${STAGE_DIR}/probe_rg477m"
chmod +x "${STAGE_DIR}/probe_rg477m"
echo "Staged binary to: ${STAGE_DIR}/probe_rg477m"

# 6. Check Device execution status
DO_PUSH=false
DO_RUN=false
for arg in "$@"; do
    case "${arg}" in
        --push) DO_PUSH=true ;;
        --run)  DO_PUSH=true; DO_RUN=true ;;
    esac
done

if [[ "${DO_PUSH}" == "true" ]]; then
    echo "Checking connected ADB devices..."
    ADB_BIN="${ANDROID_SDK_ROOT}/platform-tools/adb"
    if [[ ! -x "${ADB_BIN}" ]]; then
        ADB_BIN="$(command -v adb || true)"
    fi

    if [[ -z "${ADB_BIN}" || ! -x "${ADB_BIN}" ]]; then
        echo "BLOCKED_DEVICE: adb command not found."
    else
        DEVICE_COUNT="$("${ADB_BIN}" devices | grep -v "List of devices" | grep -c "device$" || true)"
        if [[ "${DEVICE_COUNT}" -eq 0 ]]; then
            echo "BLOCKED_DEVICE: No Android device connected via ADB."
            echo "Real RG477M device execution is blocked until device is physically attached."
        else
            echo "Pushing probe_rg477m to device /data/local/tmp/..."
            "${ADB_BIN}" push "${STAGE_DIR}/probe_rg477m" /data/local/tmp/probe_rg477m
            "${ADB_BIN}" shell chmod 755 /data/local/tmp/probe_rg477m
            if [[ "${DO_RUN}" == "true" ]]; then
                echo "Executing probe on device:"
                "${ADB_BIN}" shell /data/local/tmp/probe_rg477m --summary
            fi
        fi
    fi
fi

echo "=== Build finished successfully ==="
