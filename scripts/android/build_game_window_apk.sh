#!/usr/bin/env bash
# Build the game-window APK without gradle.
#
# aapt2 + d8 + apksigner + javac + the NDK toolchain do the whole job. Every
# step prints the real exit code of the command it ran; a failing step stops
# the build. The APK lands in .local/android-apk and stays out of git.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFRS_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export FFRS_ANDROID_BUILD_DIR="${FFRS_ANDROID_BUILD_DIR:-${FFRS_ROOT}/.local/build/android-arm64-game-window}"
export FFRS_ANDROID_LOG_DIR="${FFRS_ANDROID_LOG_DIR:-${FFRS_ROOT}/.local/logs/android-game-window}"
# Reuse the repository toolchain discovery instead of duplicating it.
# shellcheck source=../device/android_env.sh
source "${FFRS_ROOT}/scripts/device/android_env.sh"

FFRS_APK_DIR="${FFRS_ROOT}/.local/android-apk"
FFRS_BUILD_TOOLS="${FFRS_BUILD_TOOLS:-${FFRS_SDK}/build-tools/34.0.0}"
FFRS_ANDROID_JAR="${FFRS_ANDROID_JAR:-${FFRS_SDK}/platforms/android-34/android.jar}"
FFRS_APP_MANIFEST="${FFRS_ROOT}/src/android/app/gamewindow/AndroidManifest.xml"
FFRS_APP_RES="${FFRS_ROOT}/src/android/app/gamewindow/res"
FFRS_APP_JAVA="${FFRS_ROOT}/src/android/app/java/org/ffrs/diagnostic"
FFRS_HLE_DIR="${FFRS_ROOT}/src/runtime/hle"
FFRS_NATIVE_WINDOW_SOURCE="${FFRS_HLE_DIR}/native_window.c"
FFRS_NATIVE_WINDOW_HEADER="${FFRS_HLE_DIR}/native_window.h"
mkdir -p "${FFRS_ROOT}/.local/build"
FFRS_PACKAGE_DIR="$(mktemp -d "${FFRS_ROOT}/.local/build/game-window-apk.XXXXXX")"
FFRS_APK_NAME="ffrs-game-window-debug.apk"

# Runs one build step, keeps its full output, and reports the real exit code.
step() {
    local label="$1"
    shift
    printf '=== [%s] %s\n' "${label}" "$*"
    local result=0
    "$@" > "${FFRS_LOGS}/${label}.log" 2>&1 || result=$?
    printf '%s\n' "${result}" > "${FFRS_LOGS}/${label}.exitcode"
    printf 'exit_code[%s]=%s\n' "${label}" "${result}"
    cat "${FFRS_LOGS}/${label}.log"
    if [[ "${result}" != 0 ]]; then
        echo "BUILD_FAILED: ${label} exited with ${result}" >&2
        exit "${result}"
    fi
}

printf 'root=%s\nbuild_tools=%s\nandroid_jar=%s\nndk=%s\nllvm=%s\n' \
    "${FFRS_ROOT}" "${FFRS_BUILD_TOOLS}" "${FFRS_ANDROID_JAR}" "${FFRS_NDK}" "${FFRS_LLVM}"
for executable in aapt2 d8 zipalign apksigner; do
    if [[ ! -x "${FFRS_BUILD_TOOLS}/${executable}" ]]; then
        echo "BLOCKED_TOOLCHAIN: missing ${FFRS_BUILD_TOOLS}/${executable}" >&2
        exit 2
    fi
done
for tool in javac jar zip cmake; do
    if ! command -v "${tool}" > /dev/null 2>&1; then
        echo "BLOCKED_TOOLCHAIN: missing ${tool}" >&2
        exit 2
    fi
done
if [[ ! -f "${FFRS_ANDROID_JAR}" ]]; then
    echo "BLOCKED_TOOLCHAIN: missing Android platform jar ${FFRS_ANDROID_JAR}" >&2
    exit 2
fi
if [[ ! -f "${FFRS_NATIVE_WINDOW_SOURCE}" || ! -f "${FFRS_NATIVE_WINDOW_HEADER}" ]]; then
    echo "BLOCKED_SOURCE: missing ${FFRS_NATIVE_WINDOW_SOURCE}" >&2
    exit 2
fi

# The window holder must build for the macOS host as well, because the host
# runtime links the same translation unit. The strict flags are the ones the
# host contract build uses.
mkdir -p "${FFRS_PACKAGE_DIR}/host"
step host-compile-native-window \
    "${HOST_CC:-clang}" -std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror \
    -I "${FFRS_HLE_DIR}" -c "${FFRS_NATIVE_WINDOW_SOURCE}" \
    -o "${FFRS_PACKAGE_DIR}/host/native_window.host.o"

step cmake-configure "${FFRS_CMAKE}" -S "${FFRS_ROOT}/src/android/native" -B "${FFRS_BUILD}" \
    -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE="${FFRS_NDK}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
step cmake-build-game-window \
    "${FFRS_CMAKE}" --build "${FFRS_BUILD}" --target ffrs_game_window -j "${FFRS_BUILD_JOBS:-4}"
step android-elf-header \
    "${FFRS_LLVM}/bin/llvm-readelf" --file-header "${FFRS_BUILD}/libffrs_game_window.so"

# The window holder is compiled into the Android library from the same source
# file the host check above used.
step android-compile-native-window \
    "${FFRS_LLVM}/bin/clang" --target=aarch64-linux-android28 \
    --sysroot="${FFRS_LLVM}/sysroot" \
    -fPIC -std=c11 -Wall -Wextra -Werror -I "${FFRS_HLE_DIR}" \
    -c "${FFRS_NATIVE_WINDOW_SOURCE}" -o "${FFRS_PACKAGE_DIR}/host/native_window.android.o"

mkdir -p "${FFRS_PACKAGE_DIR}/classes" "${FFRS_PACKAGE_DIR}/dex" \
    "${FFRS_PACKAGE_DIR}/payload/lib/arm64-v8a" "${FFRS_PACKAGE_DIR}/res"
step javac javac --release 8 -classpath "${FFRS_ANDROID_JAR}" -g -d "${FFRS_PACKAGE_DIR}/classes" \
    "${FFRS_APP_JAVA}/GameActivity.java" \
    "${FFRS_APP_JAVA}/GameWindowNative.java" \
    "${FFRS_APP_JAVA}/DisplaySnapshot.java"
step jar jar cf "${FFRS_PACKAGE_DIR}/classes.jar" -C "${FFRS_PACKAGE_DIR}/classes" .
step d8 "${FFRS_BUILD_TOOLS}/d8" --min-api 28 --lib "${FFRS_ANDROID_JAR}" \
    --output "${FFRS_PACKAGE_DIR}/dex" "${FFRS_PACKAGE_DIR}/classes.jar"
step aapt2-compile "${FFRS_BUILD_TOOLS}/aapt2" compile --dir "${FFRS_APP_RES}" \
    -o "${FFRS_PACKAGE_DIR}/res/resources.zip"
step aapt2-link "${FFRS_BUILD_TOOLS}/aapt2" link \
    -o "${FFRS_PACKAGE_DIR}/base.apk" -I "${FFRS_ANDROID_JAR}" \
    --manifest "${FFRS_APP_MANIFEST}" "${FFRS_PACKAGE_DIR}/res/resources.zip"

cp "${FFRS_PACKAGE_DIR}/dex/classes.dex" "${FFRS_PACKAGE_DIR}/payload/classes.dex"
step strip "${FFRS_LLVM}/bin/llvm-strip" --strip-debug \
    -o "${FFRS_PACKAGE_DIR}/payload/lib/arm64-v8a/libffrs_game_window.so" \
    "${FFRS_BUILD}/libffrs_game_window.so"
cp "${FFRS_PACKAGE_DIR}/base.apk" "${FFRS_PACKAGE_DIR}/unsigned.apk"
step payload-zip bash -c 'cd "$1" && zip -q -r "$2" classes.dex lib' \
    _ "${FFRS_PACKAGE_DIR}/payload" "${FFRS_PACKAGE_DIR}/unsigned.apk"
step zipalign "${FFRS_BUILD_TOOLS}/zipalign" -f -p 4 \
    "${FFRS_PACKAGE_DIR}/unsigned.apk" "${FFRS_PACKAGE_DIR}/aligned.apk"

SIGNING_DIR="${FFRS_ROOT}/.local/signing"
mkdir -p "${SIGNING_DIR}"
# A new local development key only. Never reads any user/release credential.
if [[ ! -f "${SIGNING_DIR}/gamewindow-debug.p12" ]]; then
    step keytool keytool -genkeypair -keystore "${SIGNING_DIR}/gamewindow-debug.p12" \
        -storetype PKCS12 -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 3650 \
        -dname 'CN=FFRS Local Game Window,O=Development,C=US' -noprompt
fi
APK="${FFRS_APK_DIR}/${FFRS_APK_NAME}"
mkdir -p "${FFRS_APK_DIR}"
step apksigner-sign "${FFRS_BUILD_TOOLS}/apksigner" sign \
    --ks "${SIGNING_DIR}/gamewindow-debug.p12" --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android \
    --out "${APK}" "${FFRS_PACKAGE_DIR}/aligned.apk"
step apksigner-verify "${FFRS_BUILD_TOOLS}/apksigner" verify --verbose --print-certs "${APK}"
step aapt2-badging "${FFRS_BUILD_TOOLS}/aapt2" dump badging "${APK}"

step sha256 shasum -a 256 "${APK}"
printf '%s  %s\n' "$(cat "${FFRS_LOGS}/sha256.log" | awk '{print $1}')" "${FFRS_APK_NAME}" \
    > "${FFRS_APK_DIR}/${FFRS_APK_NAME}.sha256"
printf 'APK created: %s\n' "${APK}"
printf 'sha256: %s\n' "$(cat "${FFRS_LOGS}/sha256.log" | awk '{print $1}')"
printf 'Build logs: %s\n' "${FFRS_LOGS}"
