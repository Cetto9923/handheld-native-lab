#!/usr/bin/env bash
# The paired A/B of the map transition: the same save, the same path, the same operation, one arm at a time.
#
#   arms:
#     rb-drain      gfx-readback-retire=0  (the old full drain at every readback: the reference)
#     rb-target     gfx-readback-retire=1  (the readback waits for the submission that owns it)
#     defer0        gfx-defer-events=0, gfx-event-retire=1   (no deferral: the reference)
#     defer1-drain  gfx-defer-events=1, gfx-event-retire=0   (deferral with the old full drain at the read point)
#     defer1-target gfx-defer-events=1, gfx-event-retire=1   (deferral with the retirement of the event's own submission)
#     warm-plain / warm-off / warm-budget   the warm-up's share of the driver
#
# The path file is a list of steps, one per line:
#   key KEYCODE [--duration MS]   a gamepad press through the game's own input path
#   sleep SECONDS
#   note LABEL                    a mark in the device clock; TRANSITION_START and TRANSITION_END bound the window
#
# The timing comes from the buffered CSV the engine writes (codex-frame-<pid>.csv, CLOCK_MONOTONIC) and from the device
# clock read at the notes. A screen recording is never used for timing: it samples below the frame rate and changes it.
#
# The device clock at the notes is read from /proc/uptime (CLOCK_BOOTTIME). The CSV clock is CLOCK_MONOTONIC. The two are
# the same as long as the device did not suspend, which the run reports next to the window it used.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFRS_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${FFRS_ROOT}"

HONOR_SERIAL="A42BBB5109000333"
RG477M_SERIAL="RG477M01445122"
DEVICE="${FFRS_DEVICE:-${HONOR_SERIAL}}"
PACKAGE="org.ffrs.gamewindow"
ACTIVITY="org.ffrs.diagnostic.GameActivity"
FILES="/sdcard/Android/data/${PACKAGE}/files"

PATH_FILE=""
OUT_DIR=""
ARMS="rb-drain,rb-target"
WARM=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --path) PATH_FILE="$2"; shift 2;;
        --out) OUT_DIR="$2"; shift 2;;
        --arms) ARMS="$2"; shift 2;;
        --warm) WARM=1; shift;;
        --device) DEVICE="$2"; shift 2;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done

if [[ "${DEVICE}" == "${RG477M_SERIAL}" ]]; then
    echo "BLOCKED_DEVICE: operating on the RG477M is forbidden in honor-gtpro" >&2
    exit 2
fi
if [[ -z "${PATH_FILE}" || ! -f "${PATH_FILE}" ]]; then
    echo "BLOCKED_INPUT: --path must name an existing path file" >&2
    exit 2
fi
if [[ -z "${OUT_DIR}" ]]; then
    OUT_DIR="${FFRS_ROOT}/.local/transit-ab/$(date '+%Y%m%d-%H%M%S')"
fi
mkdir -p "${OUT_DIR}"
if ! adb -s "${DEVICE}" get-state >/dev/null 2>&1; then
    echo "BLOCKED_DEVICE: ${DEVICE} is not attached" >&2
    exit 2
fi

# The device clock, in seconds since boot.
device_uptime() {
    adb -s "${DEVICE}" shell cat /proc/uptime | awk '{print $1}'
}

set_marker() {
    local name="$1" value="$2"
    adb -s "${DEVICE}" shell "echo -n '${value}' > ${FILES}/${name}"
    local back
    back="$(adb -s "${DEVICE}" shell "cat ${FILES}/${name}" | tr -d '\r\n')"
    if [[ "${back}" != "${value}" ]]; then
        echo "BLOCKED_MARKER: ${name} was not set (wanted '${value}', found '${back}')" >&2
        return 1
    fi
    echo "marker ${name}=${back}"
}

remove_marker() {
    adb -s "${DEVICE}" shell "rm -f ${FILES}/$1" >/dev/null 2>&1 || true
}

# The arms: the deferral, the retirement rule, and the warm-up's share of the driver.
apply_arm() {
    local arm="$1"
    case "${arm}" in
        defer0)        set_marker gfx-defer-events 0; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 1;;
        defer1-drain)  set_marker gfx-defer-events 1; set_marker gfx-event-retire 0; set_marker gfx-readback-retire 1;;
        defer1-target) set_marker gfx-defer-events 1; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 1;;
        # The readback arms: the retirement of the submission that owns the readback, or the old full drain.
        rb-drain)      set_marker gfx-defer-events 0; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 0;;
        rb-target)     set_marker gfx-defer-events 0; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 1;;
        # The warm-up arms: the list prewarm on (the shipped behaviour), off, or paced.
        warm-off)      set_marker gfx-defer-events 0; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 1
                      adb -s "${DEVICE}" shell "touch ${FILES}/gfx-no-warm" >/dev/null
                      remove_marker gfx-warm-budget; remove_marker gfx-warm-interval;;
        warm-budget)   set_marker gfx-defer-events 0; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 1
                      remove_marker gfx-no-warm
                      set_marker gfx-warm-budget "${FFRS_WARM_BUDGET:-4}"
                      set_marker gfx-warm-interval "${FFRS_WARM_INTERVAL:-0}";;
        warm-plain)    set_marker gfx-defer-events 0; set_marker gfx-event-retire 1; set_marker gfx-readback-retire 1
                      remove_marker gfx-no-warm; remove_marker gfx-warm-budget; remove_marker gfx-warm-interval;;
        *) echo "unknown arm: ${arm}" >&2; return 1;;
    esac
    # The measurement switches are the same in every arm.
    set_marker gfx-codex-frame-metrics 1
    # The event guard makes the guest read fault, so it is never on during a timed run.
    remove_marker gfx-event-guard
}

run_path() {
    local log="$1"
    : > "${log}"
    while IFS= read -r line || [[ -n "${line}" ]]; do
        line="${line%%#*}"
        line="${line#"${line#${line%%[![:space:]]*}}"}"
        [[ -z "${line}" ]] && continue
        local stamp
        stamp="$(device_uptime)"
        printf '%s\t%s\n' "${stamp}" "${line}" >> "${log}"
        local verb rest
        verb="${line%% *}"
        rest="${line#* }"
        case "${verb}" in
            key)
                local code duration=160
                if [[ "${rest}" == *"--duration"* ]]; then
                    code="${rest%% *}"
                    duration="${rest##*--duration }"
                else
                    code="${rest}"
                fi
                adb -s "${DEVICE}" shell input gamepad keyevent --duration "${duration}" "${code}" >/dev/null 2>&1 || true
                ;;
            sleep) sleep "${rest}";;
            note) :;;
            *) echo "unknown step: ${line}" >&2; return 1;;
        esac
    done < "${PATH_FILE}"
}

wait_for_csv() {
    local pid="$1" tries=60
    while [[ ${tries} -gt 0 ]]; do
        if adb -s "${DEVICE}" shell "ls ${FILES}/codex-frame-${pid}.csv" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        tries=$((tries-1))
    done
    echo "BLOCKED_INPUT: no frame CSV for pid ${pid}" >&2
    return 1
}

summary() {
    local arm="$1" phase="$2"
    python3 - "${OUT_DIR}/${arm}-${phase}.json" "${OUT_DIR}/summary.txt" "${arm}" "${phase}" <<'PY'
import json, sys
path, summary_path, arm, phase = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
try:
    d = json.load(open(path))
except Exception as exc:
    print(f"{arm}\t{phase}\tANALYSIS_FAILED\t{exc}")
    open(summary_path, 'a').write(f"{arm}\t{phase}\tANALYSIS_FAILED\t{exc}\n")
    raise SystemExit(0)
row = (arm, phase,
       f"fps={d.get('present_fps')}",
       f"work_fps={d.get('distinct_render_work_fps')}",
       f"wall={d.get('duration_seconds')}",
       f"p50={d.get('interval_ms', {}).get('p50')}",
       f"p95={d.get('interval_ms', {}).get('p95')}",
       f"max={d.get('interval_ms', {}).get('max')}",
       f"over100ms={d.get('over_100ms')}",
       f"frames={d.get('queued_presents')}")
line = "\t".join(str(x) for x in row)
print(line)
open(summary_path, 'a').write(line + "\n")
PY
}

echo "device=${DEVICE} arms=${ARMS} warm=${WARM} out=${OUT_DIR}"
: > "${OUT_DIR}/summary.txt"

for arm in ${ARMS//,/ }; do
    phase="cold"
    [[ ${WARM} -eq 1 ]] && phase="warm"
    echo "=== arm ${arm} (${phase}) ==="
    apply_arm "${arm}" || exit 1
    if [[ ${WARM} -eq 0 ]]; then
        adb -s "${DEVICE}" shell am force-stop "${PACKAGE}"
    else
        adb -s "${DEVICE}" shell am force-stop "${PACKAGE}"
    fi
    adb -s "${DEVICE}" shell am start -W -n "${PACKAGE}/${ACTIVITY}" >/dev/null 2>&1 || true
    sleep 8
    pid="$(adb -s "${DEVICE}" shell pidof "${PACKAGE}" | tr -d '\r\n')"
    if [[ -z "${pid}" ]]; then
        echo "BLOCKED_DEVICE: the app did not start for arm ${arm}" >&2
        exit 1
    fi
    echo "pid=${pid}"
    wait_for_csv "${pid}" || exit 1
    run_path "${OUT_DIR}/${arm}-${phase}-path.tsv"
    sleep 3
    adb -s "${DEVICE}" pull "${FILES}/codex-frame-${pid}.csv" "${OUT_DIR}/${arm}-${phase}-frame.csv" >/dev/null
    adb -s "${DEVICE}" pull "${FILES}/guest_boot.log" "${OUT_DIR}/${arm}-${phase}-guest_boot.log" >/dev/null 2>&1 || true
    # The window: the notes of the path, in the device clock.
    start_ns="$(awk -F'\t' '$2 ~ /note TRANSITION_START/ {printf "%d", $1*1e9}' "${OUT_DIR}/${arm}-${phase}-path.tsv")"
    end_ns="$(awk -F'\t' '$2 ~ /note TRANSITION_END/ {printf "%d", $1*1e9}' "${OUT_DIR}/${arm}-${phase}-path.tsv")"
    echo "window_ns=${start_ns}..${end_ns}"
    if [[ -n "${start_ns}" && -n "${end_ns}" ]]; then
        python3 scripts/graphics/analyze_frame_metrics.py "${OUT_DIR}/${arm}-${phase}-frame.csv"             --label "${arm}-${phase}-transition" --start-ns "${start_ns}" --end-ns "${end_ns}"             --out "${OUT_DIR}/${arm}-${phase}.json" >/dev/null
    else
        echo "BLOCKED_INPUT: the path file has no TRANSITION_START/TRANSITION_END note" >&2
    fi
    # The acceptance metrics: the whole run, and the transition segment on its own.
    python3 scripts/graphics/analyze_stalls.py "${OUT_DIR}/${arm}-${phase}-frame.csv" --json "${OUT_DIR}/${arm}-${phase}-stalls.json" > /dev/null 2>&1 || true
    if [[ -n "${start_ns}" && -n "${end_ns}" ]]; then
        python3 scripts/graphics/analyze_stalls.py "${OUT_DIR}/${arm}-${phase}-frame.csv" --start-ns "${start_ns}" --end-ns "${end_ns}" --json "${OUT_DIR}/${arm}-${phase}-transition-stalls.json" > /dev/null 2>&1 || true
    fi
    # The engine's own counters for the same run: the deferral, the warm-up, the readback retirement, and the perf
    # windows of the run (the slow windows against the fast ones, with the fixed baseline in the analyzer).
    grep -E "NVN PERF events defer=|NVN PERF block_ms|NVN PERF readback|NVN defer events=|NVN event retirement|NVN readback retirement|NVN warm throttle|NVN warm list=|NVN warm list seeded" "${OUT_DIR}/${arm}-${phase}-guest_boot.log" > "${OUT_DIR}/${arm}-${phase}-perf.txt" 2>/dev/null || true
    python3 scripts/graphics/analyze_stalls.py --perf-log "${OUT_DIR}/${arm}-${phase}-guest_boot.log" --json "${OUT_DIR}/${arm}-${phase}-perf-windows.json" > "${OUT_DIR}/${arm}-${phase}-perf-windows.txt" 2>&1 || true
    sed -n '/^counters/,/^slowest/p' "${OUT_DIR}/${arm}-${phase}-perf-windows.txt" 2>/dev/null | head -8 || true
    grep -E "NVN PERF v=1" "${OUT_DIR}/${arm}-${phase}-guest_boot.log" | head -4 | cut -c1-200 >> "${OUT_DIR}/${arm}-${phase}-perf.txt" 2>/dev/null || true
    tail -8 "${OUT_DIR}/${arm}-${phase}-perf.txt" 2>/dev/null || true
    summary "${arm}" "${phase}"
done

# The markers are restored to the shipped defaults: the engine reads them at its start, so removing them is the
# state the device was in before the run.
for marker in gfx-defer-events gfx-event-retire gfx-readback-retire gfx-warm-budget gfx-warm-interval gfx-no-warm; do
    remove_marker "${marker}"
done
echo "the markers were removed again: the device is back at the shipped defaults"
echo "summary in ${OUT_DIR}/summary.txt"
