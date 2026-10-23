#!/usr/bin/env bash
# Offline packaging after NDK installation. Uses only installed SDK tools + JDK.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android_env.sh"
if [[ -z "${FFRS_ANDROID_LOG_DIR:-}" ]]; then
    FFRS_LOGS="$(mktemp -d "${FFRS_ROOT}/.local/logs/android-build.XXXXXX")"
    export FFRS_ANDROID_LOG_DIR="${FFRS_LOGS}"
fi
finish_build() {
    local result=$?
    trap - EXIT
    python3 "${DEVICE_SCRIPT_DIR}/record_build_evidence.py" --source-root "${FFRS_ROOT}" \
        --logs "${FFRS_LOGS}" --artifacts "${FFRS_ARTIFACTS}" --overall-exit "${result}" || result=6
    printf 'Build evidence: %s/build-evidence.json\nStep logs: %s\n' "${FFRS_ARTIFACTS}" "${FFRS_LOGS}"
    exit "${result}"
}
trap finish_build EXIT
printf 'NOT_RUN\n' > "${FFRS_LOGS}/source-guard.exitcode"
snapshot_sources() {
    python3 "${DEVICE_SCRIPT_DIR}/snapshot_build_inputs.py" --source-root "${FFRS_ROOT}" \
        --runtime "${FFRS_RUNTIME_DIR:-${FFRS_ROOT}/src/runtime/nso}" \
        --additional-dir "${FFRS_NVN_DIR:-${FFRS_ROOT}/src/graphics/nvn}"
}
snapshot_sources > "${FFRS_LOGS}/build-input-before.json"
FFRS_BUILD_TOOLS="${FFRS_BUILD_TOOLS:-${FFRS_SDK}/build-tools/34.0.0}"
FFRS_ANDROID_JAR="${FFRS_ANDROID_JAR:-${FFRS_SDK}/platforms/android-34/android.jar}"
for executable in aapt d8 zipalign apksigner; do
    if [[ ! -x "${FFRS_BUILD_TOOLS}/${executable}" ]]; then
        echo "BLOCKED_TOOLCHAIN: missing ${FFRS_BUILD_TOOLS}/${executable}" >&2
        exit 2
    fi
done
if [[ ! -f "${FFRS_ANDROID_JAR}" ]]; then
    echo "BLOCKED_TOOLCHAIN: missing Android platform jar ${FFRS_ANDROID_JAR}" >&2
    exit 2
fi
"${DEVICE_SCRIPT_DIR}/build_android_probe.sh" --all
# New packaging directory per invocation avoids stale class/native input.
PACKAGE_DIR="$(mktemp -d "${FFRS_BUILD}/apk.XXXXXX")"
mkdir -p "${PACKAGE_DIR}/classes" "${PACKAGE_DIR}/dex" "${PACKAGE_DIR}/payload/lib/arm64-v8a"
ffrs_run javac javac --release 8 -classpath "${FFRS_ANDROID_JAR}" -g -d "${PACKAGE_DIR}/classes" \
    "${FFRS_ROOT}/src/android/app/java/org/ffrs/diagnostic/DiagnosticActivity.java"
ffrs_run jar jar cf "${PACKAGE_DIR}/classes.jar" -C "${PACKAGE_DIR}/classes" .
ffrs_run dex "${FFRS_BUILD_TOOLS}/d8" --min-api 28 --lib "${FFRS_ANDROID_JAR}" --output "${PACKAGE_DIR}/dex" "${PACKAGE_DIR}/classes.jar"
ffrs_run package-manifest "${FFRS_BUILD_TOOLS}/aapt" package -f \
    -M "${FFRS_ROOT}/src/android/app/AndroidManifest.xml" -S "${FFRS_ROOT}/src/android/app/res" \
    -I "${FFRS_ANDROID_JAR}" -F "${PACKAGE_DIR}/unsigned.apk"
cp "${PACKAGE_DIR}/dex/classes.dex" "${PACKAGE_DIR}/payload/classes.dex"
ffrs_run strip "${FFRS_LLVM}/bin/llvm-strip" --strip-debug -o "${PACKAGE_DIR}/payload/lib/arm64-v8a/libffrs_diagnostic.so" "${FFRS_BUILD}/libffrs_diagnostic.so"
ffrs_run payload-zip bash -c 'cd "$1" && zip -q -r "$2" classes.dex lib' _ "${PACKAGE_DIR}/payload" "${PACKAGE_DIR}/unsigned.apk"
ffrs_run zipalign "${FFRS_BUILD_TOOLS}/zipalign" -f -p 4 "${PACKAGE_DIR}/unsigned.apk" "${PACKAGE_DIR}/aligned.apk"
snapshot_sources > "${FFRS_LOGS}/build-input-after-compile.json"
if ! cmp -s "${FFRS_LOGS}/build-input-before.json" "${FFRS_LOGS}/build-input-after-compile.json"; then
    printf '5\n' > "${FFRS_LOGS}/source-guard.exitcode"
    echo 'SOURCE_CHANGED: build input changed during compilation; candidate APK is not accepted.' >&2
    exit 5
fi
SIGNING_DIR="${FFRS_ROOT}/.local/signing"
mkdir -p "${SIGNING_DIR}"
# A new local development key only. Never reads any user/release credential.
if [[ ! -f "${SIGNING_DIR}/diagnostic-debug.p12" ]]; then
    keytool -genkeypair -keystore "${SIGNING_DIR}/diagnostic-debug.p12" -storetype PKCS12 \
        -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 \
        -validity 3650 -dname 'CN=FFRS Local Diagnostic,O=Development,C=US' -noprompt \
        > "${FFRS_LOGS}/debug-key-generation.log" 2>&1
fi
APK="${FFRS_ARTIFACTS}/ffrs-diagnostic-debug.apk"
sign_result=0
"${FFRS_BUILD_TOOLS}/apksigner" sign --ks "${SIGNING_DIR}/diagnostic-debug.p12" --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android --out "${APK}" "${PACKAGE_DIR}/aligned.apk" \
    > "${FFRS_LOGS}/sign.log" 2>&1 || sign_result=$?
printf '%s\n' "${sign_result}" > "${FFRS_LOGS}/sign.exitcode"
if [[ "${sign_result}" != 0 ]]; then cat "${FFRS_LOGS}/sign.log"; exit "${sign_result}"; fi
ffrs_run apk-signature "${FFRS_BUILD_TOOLS}/apksigner" verify --verbose --print-certs "${APK}"
ffrs_run apk-manifest "${FFRS_BUILD_TOOLS}/aapt" dump badging "${APK}"
ffrs_run apk-structure python3 "${FFRS_ROOT}/tests/device/verify_android_artifacts.py" \
    --apk "${APK}" --readelf "${FFRS_LLVM}/bin/llvm-readelf"
if [[ -n "${FFRS_REVIEWED_LEAF_LIBRARY:-}" ]]; then
    ffrs_run leaf-not-embedded python3 "${FFRS_ROOT}/tests/device/verify_leaf_exclusion.py" \
        --apk "${APK}" --reviewed-library "${FFRS_REVIEWED_LEAF_LIBRARY}"
fi
snapshot_sources > "${FFRS_LOGS}/build-input-after.json"
if ! cmp -s "${FFRS_LOGS}/build-input-before.json" "${FFRS_LOGS}/build-input-after.json"; then
    printf '5\n' > "${FFRS_LOGS}/source-guard.exitcode"
    echo 'SOURCE_CHANGED: build input changed before final verification; candidate APK is not accepted.' >&2
    exit 5
fi
printf '0\n' > "${FFRS_LOGS}/source-guard.exitcode"
printf 'APK created: %s\nUnstripped native symbols: %s\n' "${APK}" "${FFRS_BUILD}/libffrs_diagnostic.so"
