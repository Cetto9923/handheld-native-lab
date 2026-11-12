#!/usr/bin/env bash
# RG477M texel storage verification: install the game window APK, launch it and
# collect the engine log, the fatal marker and a screenshot.
#
# The RG477M is the only allowed target. The Honor tablet is never touched and
# adb kill-server is never used.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFRS_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FFRS_DEVICE="${1:-RG477M01445122}"
FFRS_APK="${FFRS_APK:-${FFRS_ROOT}/.local/android-apk/ffrs-game-window-debug.apk}"
FFRS_OUT="${FFRS_OUT:-${FFRS_ROOT}/.local/rg477m-texel-20261112/device}"
FFRS_PACKAGE="org.ffrs.gamewindow"
FFRS_ACTIVITY="org.ffrs.diagnostic.GameActivity"
FFRS_SECONDS="${FFRS_SECONDS:-45}"
mkdir -p "${FFRS_OUT}"

if [[ "${FFRS_DEVICE}" != "RG477M01445122" ]]; then
    echo "BLOCKED_DEVICE: only RG477M01445122 is allowed, got ${FFRS_DEVICE}" >&2
    exit 2
fi
if ! adb devices | awk 'NR>1 && $2=="device" {print $1}' | grep -qx "${FFRS_DEVICE}"; then
    echo "BLOCKED_DEVICE: ${FFRS_DEVICE} is not attached" >&2
    adb devices -l >&2
    exit 2
fi
if [[ ! -f "${FFRS_APK}" ]]; then
    echo "BLOCKED_INPUT: missing ${FFRS_APK}" >&2
    exit 2
fi

adb -s "${FFRS_DEVICE}" install -r "${FFRS_APK}" > "${FFRS_OUT}/install.log" 2>&1
adb -s "${FFRS_DEVICE}" shell am force-stop "${FFRS_PACKAGE}"
adb -s "${FFRS_DEVICE}" shell rm -f /sdcard/Android/data/${FFRS_PACKAGE}/files/engine.log.fatal
adb -s "${FFRS_DEVICE}" logcat -c
adb -s "${FFRS_DEVICE}" shell am start -W -n "${FFRS_PACKAGE}/${FFRS_ACTIVITY}" > "${FFRS_OUT}/launch.log" 2>&1
sleep "${FFRS_SECONDS}"
adb -s "${FFRS_DEVICE}" shell "cat /sdcard/Android/data/${FFRS_PACKAGE}/files/engine.log" > "${FFRS_OUT}/engine.log" 2>&1 || true
adb -s "${FFRS_DEVICE}" shell "cat /sdcard/Android/data/${FFRS_PACKAGE}/files/engine.log.fatal" > "${FFRS_OUT}/engine.log.fatal" 2>&1 || true
adb -s "${FFRS_DEVICE}" logcat -d -t 400 > "${FFRS_OUT}/logcat.txt" 2>&1 || true
adb -s "${FFRS_DEVICE}" exec-out screencap -p > "${FFRS_OUT}/screen.png" || true

echo "--- texel lines ---"
grep -E "texel|Texel" "${FFRS_OUT}/engine.log" | tail -20 || true
echo "--- typed storage variant ---"
grep -E "typed storage" "${FFRS_OUT}/engine.log" | tail -20 || true
echo "--- fatal marker ---"
if [[ -s "${FFRS_OUT}/engine.log.fatal" ]]; then
    echo "FATAL_PRESENT"
    cat "${FFRS_OUT}/engine.log.fatal"
else
    echo "FATAL_ABSENT"
fi
echo "--- formatless-write FATAL ---"
if grep -q "lacks formatless-write support" "${FFRS_OUT}/engine.log"; then
    echo "FORMATLESS_WRITE_FATAL_PRESENT"
else
    echo "FORMATLESS_WRITE_FATAL_ABSENT"
fi
echo "outputs: ${FFRS_OUT}"
