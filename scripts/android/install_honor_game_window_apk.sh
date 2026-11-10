#!/usr/bin/env bash
# Install and verify game-window APK on Honor GT Pro (serial A42BBB5109000333 only).
# Pure cold start verification with automatic data discovery. No fake intent extras.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFRS_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
HONOR_SERIAL="A42BBB5109000333"
RG477M_SERIAL="RG477M01445122"

FFRS_DEVICE="${FFRS_DEVICE:-${HONOR_SERIAL}}"
FFRS_APK="${FFRS_APK:-${FFRS_ROOT}/.local/android-apk/ffrs-game-window-debug.apk}"
FFRS_PACKAGE="org.ffrs.gamewindow"
FFRS_ACTIVITY="org.ffrs.diagnostic.GameActivity"
FFRS_LOGS="${FFRS_ROOT}/.local/logs/android-game-window"
mkdir -p "${FFRS_LOGS}"

# Device isolation enforcement
if [[ "${FFRS_DEVICE}" == "${RG477M_SERIAL}" ]]; then
    echo "BLOCKED_DEVICE: Operating on RG477M01445122 is strictly forbidden in honor-gtpro!" >&2
    exit 2
fi

if [[ ! -f "${FFRS_APK}" ]]; then
    echo "BLOCKED_INPUT: missing ${FFRS_APK}; run scripts/android/build_game_window_apk.sh first" >&2
    exit 2
fi

if ! adb -s "${FFRS_DEVICE}" get-state >/dev/null 2>&1; then
    echo "BLOCKED_DEVICE: ${FFRS_DEVICE} is not attached or ready" >&2
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

echo "Target Device: ${FFRS_DEVICE} (Honor GT Pro)"
step device-properties adb -s "${FFRS_DEVICE}" shell "getprop ro.product.model; getprop ro.build.version.sdk"
step install adb -s "${FFRS_DEVICE}" install -r "${FFRS_APK}"
step force-stop adb -s "${FFRS_DEVICE}" shell am force-stop "${FFRS_PACKAGE}"
step logcat-clear adb -s "${FFRS_DEVICE}" logcat -c

# Launch without any Intent extras: pure cold-start autodiscovery
step cold-launch adb -s "${FFRS_DEVICE}" shell am start -W -n "${FFRS_PACKAGE}/${FFRS_ACTIVITY}"
sleep 3
step logcat-autodiscover adb -s "${FFRS_DEVICE}" logcat -d -s FFRS-GameWindow:I '*:S'

printf '\nAutodiscover verification log:\n'
grep -E "game_dir=|resolveGameDir|guest_image_unreadable|guest_paks_missing" "${FFRS_LOGS}/logcat-autodiscover.log" || true

printf '\nEvidence stored in: %s\n' "${FFRS_LOGS}"
