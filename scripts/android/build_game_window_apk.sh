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

FFRS_APK_DIR="${FFRS_APK_DIR:-${FFRS_ROOT}/.local/android-apk}"
FFRS_BUILD_TOOLS="${FFRS_BUILD_TOOLS:-${FFRS_SDK}/build-tools/34.0.0}"
# d8 8.2.2 (build-tools 34.0.0) dies with an internal NPE while dexing the anonymous
# OnClickListener that captures the cheat-dialog fields (org/ffrs/diagnostic/GameActivity$1):
# "Cannot invoke String.length() because <parameter1> is null". A newer d8 dexes the same
# classes, so use the newest d8 that is installed and only fall back to the pinned
# build-tools when no newer one exists. aapt2/zipalign/apksigner stay on FFRS_BUILD_TOOLS.
FFRS_D8="${FFRS_D8:-$(ls -1d "${FFRS_SDK}"/build-tools/*/d8 2>/dev/null | awk -F/ '{v=$(NF-1);split(v,a,".");print a[1]*100000+a[2]*1000+a[3], $0}' | sort -n | tail -1 | cut -d' ' -f2-)}"
FFRS_D8="${FFRS_D8:-${FFRS_BUILD_TOOLS}/d8}"
FFRS_ANDROID_JAR="${FFRS_ANDROID_JAR:-${FFRS_SDK}/platforms/android-34/android.jar}"
FFRS_APP_MANIFEST="${FFRS_APP_MANIFEST:-${FFRS_ROOT}/src/android/app/gamewindow/AndroidManifest.xml}"
FFRS_APP_RES="${FFRS_APP_RES:-${FFRS_ROOT}/src/android/app/gamewindow/res}"
FFRS_APP_JAVA="${FFRS_ROOT}/src/android/app/java/org/ffrs/diagnostic"
FFRS_HLE_DIR="${FFRS_ROOT}/src/runtime/hle"
FFRS_NATIVE_WINDOW_SOURCE="${FFRS_HLE_DIR}/native_window.c"
FFRS_NATIVE_WINDOW_HEADER="${FFRS_HLE_DIR}/native_window.h"
mkdir -p "${FFRS_ROOT}/.local/build"
FFRS_PACKAGE_DIR="$(mktemp -d "${FFRS_ROOT}/.local/build/game-window-apk.XXXXXX")"
FFRS_APK_NAME="${FFRS_APK_NAME:-ffrs-game-window-debug.apk}"
if [[ -z "${FFRS_SPIRV_TOOLS_SOURCE:-}" && -f "${FFRS_ROOT}/scripts/android/prepare_honor_spirv_tools.sh" ]]; then
    FFRS_SPIRV_TOOLS_SOURCE="$(bash "${FFRS_ROOT}/scripts/android/prepare_honor_spirv_tools.sh" 2>/dev/null || true)"
fi

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
if [[ ! -x "${FFRS_D8}" ]]; then
    echo "BLOCKED_TOOLCHAIN: missing ${FFRS_D8}" >&2
    exit 2
fi
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
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DFFRS_SPIRV_TOOLS_SOURCE="${FFRS_SPIRV_TOOLS_SOURCE:-}"
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
    "${FFRS_APP_JAVA}/RegressionSaveRoot.java" \
    "${FFRS_APP_JAVA}/DeviceProfile.java" \
    "${FFRS_APP_JAVA}/GuestStartState.java" \
    "${FFRS_APP_JAVA}/GameWindowNative.java" \
    "${FFRS_APP_JAVA}/DisplaySnapshot.java" \
    "${FFRS_APP_JAVA}/NativeDataBundle.java" \
    "${FFRS_APP_JAVA}/DataBundleInstaller.java" \
    "${FFRS_APP_JAVA}/SaveExporter.java" \
    "${FFRS_APP_JAVA}/SaveRestorer.java" \
    "${FFRS_APP_JAVA}/SaveCheater.java"
step jar jar cf "${FFRS_PACKAGE_DIR}/classes.jar" -C "${FFRS_PACKAGE_DIR}/classes" .
step d8 "${FFRS_D8}" --min-api 28 --lib "${FFRS_ANDROID_JAR}" \
    --output "${FFRS_PACKAGE_DIR}/dex" "${FFRS_PACKAGE_DIR}/classes.jar"
step aapt2-compile "${FFRS_BUILD_TOOLS}/aapt2" compile --dir "${FFRS_APP_RES}" \
    -o "${FFRS_PACKAGE_DIR}/res/resources.zip"
# An optimizer-enabled APK must carry its dependency notices even when the
# caller did not supply an assets directory. Merge only into package staging.
if [[ -n "${FFRS_SPIRV_TOOLS_SOURCE:-}" ]]; then
    FFRS_NOTICE_ASSETS="${FFRS_PACKAGE_DIR}/notice-assets"
    mkdir -p "${FFRS_NOTICE_ASSETS}"
    if [[ -n "${FFRS_APP_ASSETS:-}" ]]; then
        cp -R "${FFRS_APP_ASSETS}/." "${FFRS_NOTICE_ASSETS}/"
    fi
    FFRS_NOTICE_DIR="${FFRS_NOTICE_ASSETS}/licenses/ffrs-subgroup-inline"
    if [[ -e "${FFRS_NOTICE_DIR}" ]]; then
        echo "BLOCKED_ASSETS: license asset path already exists: ${FFRS_NOTICE_DIR}" >&2
        exit 2
    fi
    FFRS_SPIRV_REPO="$(git -C "${FFRS_SPIRV_TOOLS_SOURCE}" rev-parse --show-toplevel)"
    mkdir -p "${FFRS_NOTICE_DIR}"
    cp "${FFRS_ROOT}/NOTICE.md" "${FFRS_NOTICE_DIR}/FFRS-NOTICE.md"
    cp "${FFRS_SPIRV_REPO}/LICENSE" "${FFRS_NOTICE_DIR}/d2r-mobile-MIT.txt"
    cp "${FFRS_SPIRV_TOOLS_SOURCE}/LICENSE" "${FFRS_NOTICE_DIR}/SPIRV-Tools-Apache-2.0.txt"
    cp "${FFRS_SPIRV_TOOLS_SOURCE}/spirv-headers/LICENSE" "${FFRS_NOTICE_DIR}/SPIRV-Headers-license.txt"
    FFRS_APP_ASSETS="${FFRS_NOTICE_ASSETS}"
fi
# The cheat editor needs the 32 byte SaveDataEncryptionKey. The key is a build
# input, not a source file: it lives outside git and is staged into the
# package assets here so the key never lands in a tracked path.
FFRS_SAVE_KEY_FILE="${FFRS_SAVE_KEY_FILE:-${FFRS_ROOT}/.local/cheat/save_key.txt}"
if [[ -f "${FFRS_SAVE_KEY_FILE}" ]]; then
    FFRS_CHEAT_ASSETS="${FFRS_PACKAGE_DIR}/cheat-assets"
    mkdir -p "${FFRS_CHEAT_ASSETS}"
    cp "${FFRS_SAVE_KEY_FILE}" "${FFRS_CHEAT_ASSETS}/ffrs_save_key.bin"
    if [[ -n "${FFRS_APP_ASSETS:-}" ]]; then
        FFRS_MERGE_ASSETS="${FFRS_PACKAGE_DIR}/merged-assets"
        mkdir -p "${FFRS_MERGE_ASSETS}"
        cp -R "${FFRS_APP_ASSETS}/." "${FFRS_MERGE_ASSETS}/"
        cp -R "${FFRS_CHEAT_ASSETS}/." "${FFRS_MERGE_ASSETS}/"
        FFRS_APP_ASSETS="${FFRS_MERGE_ASSETS}"
    else
        FFRS_APP_ASSETS="${FFRS_CHEAT_ASSETS}"
    fi
    echo "cheat_assets: staged the save key from ${FFRS_SAVE_KEY_FILE}"
else
    echo "cheat_assets: no save key at ${FFRS_SAVE_KEY_FILE}; the cheat menu stays disabled"
fi
if [[ -n "${FFRS_APP_ASSETS:-}" ]]; then
    step aapt2-link "${FFRS_BUILD_TOOLS}/aapt2" link \
        -o "${FFRS_PACKAGE_DIR}/base.apk" -I "${FFRS_ANDROID_JAR}" \
        --manifest "${FFRS_APP_MANIFEST}" -A "${FFRS_APP_ASSETS}" "${FFRS_PACKAGE_DIR}/res/resources.zip"
else
    step aapt2-link "${FFRS_BUILD_TOOLS}/aapt2" link \
        -o "${FFRS_PACKAGE_DIR}/base.apk" -I "${FFRS_ANDROID_JAR}" \
        --manifest "${FFRS_APP_MANIFEST}" "${FFRS_PACKAGE_DIR}/res/resources.zip"
fi

cp "${FFRS_PACKAGE_DIR}/dex/classes.dex" "${FFRS_PACKAGE_DIR}/payload/classes.dex"
step strip "${FFRS_LLVM}/bin/llvm-strip" --strip-debug \
    -o "${FFRS_PACKAGE_DIR}/payload/lib/arm64-v8a/libffrs_game_window.so" \
    "${FFRS_BUILD}/libffrs_game_window.so"
if [[ -n "${FFRS_APP_WRAP_SCRIPT:-}" ]]; then
    [[ -f "${FFRS_APP_WRAP_SCRIPT}" ]] || { echo "Missing diagnostic wrap script" >&2; exit 2; }
    cp "${FFRS_APP_WRAP_SCRIPT}" "${FFRS_PACKAGE_DIR}/payload/lib/arm64-v8a/wrap.sh"
    chmod 755 "${FFRS_PACKAGE_DIR}/payload/lib/arm64-v8a/wrap.sh"
fi
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
