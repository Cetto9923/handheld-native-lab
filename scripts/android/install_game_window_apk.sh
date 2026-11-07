#!/usr/bin/env bash
# Install and launch the game-window APK on a real device and capture the
# native window evidence from logcat.
#
# No root, no flashing, no device data is cleared. Only the game-window
# application this project owns is installed and stopped.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFRS_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FFRS_DEVICE="${FFRS_DEVICE:-A42BBB5109000333}"
if [[ "${FFRS_DEVICE}" == "RG477M01445122" ]]; then
    echo "BLOCKED_DEVICE: Operating on RG477M01445122 is strictly forbidden in honor-gtpro repository!" >&2
    exit 2
fi
FFRS_APK="${FFRS_APK:-${FFRS_ROOT}/.local/android-apk/ffrs-game-window-debug.apk}"
FFRS_PACKAGE="org.ffrs.gamewindow"
FFRS_ACTIVITY="org.ffrs.diagnostic.GameActivity"
FFRS_LOGS="${FFRS_ANDROID_LOG_DIR:-${FFRS_ROOT}/.local/logs/android-game-window}"
mkdir -p "${FFRS_LOGS}"

if [[ ! -f "${FFRS_APK}" ]]; then
    echo "BLOCKED_INPUT: missing ${FFRS_APK}; run scripts/android/build_game_window_apk.sh first" >&2
    exit 2
fi
if ! adb devices | awk 'NR>1 && $2=="device" {print $1}' | grep -qx "${FFRS_DEVICE}"; then
    echo "BLOCKED_DEVICE: ${FFRS_DEVICE} is not attached as 'device'" >&2
    adb devices -l >&2
    exit 2
fi

step() {
    local label="$1"
    shift
    printf '=== [%s] %s\n' "${label}" "$*"
    local result=0
    "$@" > "${FFRS_LOGS}/${label}.log" 2>&1 || result=$?
    printf '%s\n' "${result}" > "${FFRS_LOGS}/${label}.exitcode"
    printf 'exit_code[%s]=%s\n' "${label}" "${result}"
    cat "${FFRS_LOGS}/${label}.log"
    return "${result}"
}

step device-properties adb -s "${FFRS_DEVICE}" shell getprop ro.build.version.sdk
step install adb -s "${FFRS_DEVICE}" install -r "${FFRS_APK}"
step force-stop adb -s "${FFRS_DEVICE}" shell am force-stop "${FFRS_PACKAGE}"
step logcat-clear adb -s "${FFRS_DEVICE}" logcat -c
step launch adb -s "${FFRS_DEVICE}" shell am start -W \
    -n "${FFRS_PACKAGE}/${FFRS_ACTIVITY}"
sleep 3
step logcat-window adb -s "${FFRS_DEVICE}" logcat -d -s FFRS-GameWindow:I '*:S'
step logcat-crash adb -s "${FFRS_DEVICE}" logcat -d -b crash -t 200
step window-dumpsys adb -s "${FFRS_DEVICE}" shell dumpsys window windows

# The owned guest boots inside this app process and writes its transcript to
# the app's own external directory. It needs time to reach the graphics
# device, so the wait is explicit instead of implied.
if [[ "${FFRS_GUEST_WAIT_SECONDS:-0}" != "0" ]]; then
    sleep "${FFRS_GUEST_WAIT_SECONDS}"
fi
step logcat-guest adb -s "${FFRS_DEVICE}" logcat -d -s FFRS-GuestBoot:I '*:S'
step app-pid adb -s "${FFRS_DEVICE}" shell pidof "${FFRS_PACKAGE}"
step guest-transcript adb -s "${FFRS_DEVICE}" shell \
    "cat /sdcard/Android/data/${FFRS_PACKAGE}/files/guest_boot.log 2>/dev/null | tail -n 200"

printf 'Device evidence: %s\n' "${FFRS_LOGS}"
