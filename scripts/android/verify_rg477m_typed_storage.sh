#!/usr/bin/env bash
# RG477M texel storage verification: install the game window APK, launch it and
# collect the engine log, the fatal marker and a screenshot.
#
# The RG477M is the only allowed target. The Honor tablet is never touched and
# adb kill-server is never used.
#
# One run answers three questions with the same log:
#   1. is the FATAL marker absent,
#   2. is the formatless-write FATAL absent,
#   3. how many shaders the typed storage rewrite covered and how many it
#      refused, with the refusal reasons in the vocabulary of the offline
#      coverage scan (coordination/results/RG477M-TEXEL-COVERAGE-20261113.md).
#
# The counts come from the engine log lines
#   NVN typed storage variant shader=<key> identity=<key>:stb1:<binding>_<format>
#   NVN typed storage UNSUPPORTED shader=<key> binding=<b> format=<f> reason=<why>
# The exit code is 0 only when no FATAL marker was written and no
# formatless-write FATAL is in the log.
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

# The refusal reasons the shipping rewrite can report. A reason outside this
# set is a new class and is printed as UNKNOWN_REASON so it cannot hide.
FFRS_REASONS=(image-type-shared-with-other-format formatless-read-on-same-image duplicate-type-not-reusable sampled-type-class-mismatch extended-format-not-supported unknown-sampled-type typed-with-other-format already-typed no-storage-image-for-binding invalid-target-format malformed-module identity-size)

# The census of one engine log: the hits, the refusals with their reason and
# the fatal markers. It also runs on a log that was collected earlier, with
# FFRS_LOG=<path>, so the numbers of an old run can be read again.
typed_storage_census() {
    local LOG="$1"
    local SUMMARY="$2"
    local HITS HIT_SHADERS REFUSED REFUSED_SHADERS count reason unknown
    HITS="$(grep -c "NVN typed storage variant shader=" "${LOG}" || true)"
    HIT_SHADERS="$(grep -o "NVN typed storage variant shader=[0-9a-f]*" "${LOG}" | sed 's/.*shader=//' | sort -u | wc -l | tr -d ' ')"
    REFUSED="$(grep -c "NVN typed storage UNSUPPORTED" "${LOG}" || true)"
    REFUSED_SHADERS="$(grep -o "NVN typed storage UNSUPPORTED shader=[0-9a-f]*" "${LOG}" | sed 's/.*shader=//' | sort -u | wc -l | tr -d ' ')"
    {
        echo "log=${LOG}"
        echo "hits=${HITS}"
        echo "hit_shaders=${HIT_SHADERS}"
        echo "refused=${REFUSED}"
        echo "refused_shaders=${REFUSED_SHADERS}"
        for reason in "${FFRS_REASONS[@]}"; do
            count="$(grep -c "NVN typed storage UNSUPPORTED .*reason=${reason}" "${LOG}" || true)"
            echo "reason_${reason}=${count}"
        done
        # A reason outside the known set is a new class: it is reported, never
        # folded into a known one.
        local seen=""
        for reason in $(grep -o "NVN typed storage UNSUPPORTED .*reason=[a-z0-9-]*" "${LOG}" | sed 's/.*reason=//' | sort -u); do
            known=0
            for candidate in "${FFRS_REASONS[@]}"; do
                if [[ "${reason}" == "${candidate}" ]]; then known=1; break; fi
            done
            if [[ ${known} -eq 0 ]]; then
                count="$(grep -c "NVN typed storage UNSUPPORTED .*reason=${reason}" "${LOG}" || true)"
                echo "reason_${reason}=${count}"
            fi
        done
        echo "fatal_marker=$([ -s "${FFRS_OUT}/engine.log.fatal" ] && echo present || echo absent)"
        echo "formatless_write_fatal=$(grep -q "lacks formatless-write support" "${LOG}" && echo present || echo absent)"
        echo "other_fatals=$(grep -c "^FATAL:" "${LOG}" || true)"
    } > "${SUMMARY}"
    echo "--- typed storage census ---"
    cat "${SUMMARY}"
    echo "--- refused shaders ---"
    grep -o "NVN typed storage UNSUPPORTED shader=[0-9a-f]* .*reason=[a-z0-9-]*" "${LOG}" | sed 's/NVN typed storage UNSUPPORTED //' | sort -u | head -40 || true
    echo "--- hit shaders ---"
    grep -o "NVN typed storage variant shader=[0-9a-f]* identity=[^ ]*" "${LOG}" | sort -u | head -40 || true
    echo "--- other fatals ---"
    grep -E "^FATAL:" "${LOG}" | head -20 || true
    if [[ -s "${FFRS_OUT}/engine.log.fatal" ]] || grep -q "lacks formatless-write support" "${LOG}"; then
        return 1
    fi
    return 0
}

if [[ -n "${FFRS_LOG:-}" ]]; then
    echo "CENSUS_ONLY: reading ${FFRS_LOG}"
    typed_storage_census "${FFRS_LOG}" "${FFRS_OUT}/typed_storage_summary.txt"
    exit $?
fi

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

LOG="${FFRS_OUT}/engine.log"
SUMMARY="${FFRS_OUT}/typed_storage_summary.txt"


echo "--- texel lines ---"
grep -E "texel|Texel" "${LOG}" | tail -20 || true
echo "--- typed storage lines ---"
grep -E "typed storage" "${LOG}" | tail -40 || true
echo "--- fatal marker ---"
if [[ -s "${FFRS_OUT}/engine.log.fatal" ]]; then
    echo "FATAL_PRESENT"
    cat "${FFRS_OUT}/engine.log.fatal"
else
    echo "FATAL_ABSENT"
fi
echo "--- formatless-write FATAL ---"
if grep -q "lacks formatless-write support" "${LOG}"; then
    echo "FORMATLESS_WRITE_FATAL_PRESENT"
else
    echo "FORMATLESS_WRITE_FATAL_ABSENT"
fi

typed_storage_census "${LOG}" "${SUMMARY}"
{
    echo "device=${FFRS_DEVICE}"
    echo "apk=${FFRS_APK}"
    echo "apk_sha256=$(shasum -a 256 "${FFRS_APK}" | awk '{print $1}')"
    echo "seconds=${FFRS_SECONDS}"
} >> "${SUMMARY}"
echo "outputs: ${FFRS_OUT}"

if [[ -s "${FFRS_OUT}/engine.log.fatal" ]]; then
    exit 1
fi
if grep -q "lacks formatless-write support" "${LOG}"; then
    exit 1
fi
exit 0
