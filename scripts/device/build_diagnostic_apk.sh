#!/usr/bin/env bash
# Offline packaging after NDK installation. Uses only installed SDK tools + JDK.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android_env.sh"
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
jar cf "${PACKAGE_DIR}/classes.jar" -C "${PACKAGE_DIR}/classes" .
ffrs_run dex "${FFRS_BUILD_TOOLS}/d8" --min-api 28 --lib "${FFRS_ANDROID_JAR}" --output "${PACKAGE_DIR}/dex" "${PACKAGE_DIR}/classes.jar"
ffrs_run package-manifest "${FFRS_BUILD_TOOLS}/aapt" package -f \
    -M "${FFRS_ROOT}/src/android/app/AndroidManifest.xml" -I "${FFRS_ANDROID_JAR}" -F "${PACKAGE_DIR}/unsigned.apk"
cp "${PACKAGE_DIR}/dex/classes.dex" "${PACKAGE_DIR}/payload/classes.dex"
"${FFRS_LLVM}/bin/llvm-strip" --strip-debug -o "${PACKAGE_DIR}/payload/lib/arm64-v8a/libffrs_diagnostic.so" "${FFRS_BUILD}/libffrs_diagnostic.so"
(cd "${PACKAGE_DIR}/payload" && zip -q -r "${PACKAGE_DIR}/unsigned.apk" classes.dex lib)
ffrs_run zipalign "${FFRS_BUILD_TOOLS}/zipalign" -f -p 4 "${PACKAGE_DIR}/unsigned.apk" "${PACKAGE_DIR}/aligned.apk"
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
"${FFRS_BUILD_TOOLS}/apksigner" sign --ks "${SIGNING_DIR}/diagnostic-debug.p12" --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android --out "${APK}" "${PACKAGE_DIR}/aligned.apk" \
    > "${FFRS_LOGS}/sign.log" 2>&1
ffrs_run apk-signature "${FFRS_BUILD_TOOLS}/apksigner" verify --verbose --print-certs "${APK}"
ffrs_run apk-manifest "${FFRS_BUILD_TOOLS}/aapt" dump badging "${APK}"
ffrs_run apk-structure python3 "${FFRS_ROOT}/tests/device/verify_android_artifacts.py" \
    --apk "${APK}" --readelf "${FFRS_LLVM}/bin/llvm-readelf"
printf 'APK created: %s\nUnstripped native symbols: %s\n' "${APK}" "${FFRS_BUILD}/libffrs_diagnostic.so"
